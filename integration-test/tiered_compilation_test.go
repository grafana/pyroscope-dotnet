package integrationtest

import (
	"encoding/json"
	"fmt"
	"net/http"
	"runtime"
	"sync"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"
	"pyroscope-dotnet-integration-test/require"
)

// Regression for https://github.com/grafana/pyroscope-dotnet/issues/480.
func TestManagedCodeCacheTieredCompilation(t *testing.T) {
	if runtime.GOOS != "linux" || runtime.GOARCH != "arm64" {
		t.Skip("exercises ARM64 managed-code classification and unwinding")
	}
	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.tiering.%d", time.Now().UnixNano())
	appURL := startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":                   appName,
		"PYROSCOPE_PROFILING_CPU_ENABLED":              "true",
		"PYROSCOPE_PROFILING_WALLTIME_ENABLED":         "false",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED":       "false",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED":       "false",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED":        "false",
		"PYROSCOPE_PROFILING_HEAP_ENABLED":             "false",
		"DD_INTERNAL_PROFILING_USE_MANAGED_CODE_CACHE": "1",
		"DD_INTERNAL_CPU_PROFILER_TYPE":                "TimerCreate",
		"DD_PROFILING_UPLOAD_PERIOD":                   "2",
		"DOTNET_ReadyToRun":                            "0",
		"DOTNET_TieredCompilation":                     "1",
		"DOTNET_TieredPGO":                             "1",
		"DOTNET_TC_QuickJit":                           "1",
		"DOTNET_TC_QuickJitForLoops":                   "1",
		"DOTNET_TC_CallCountThreshold":                 "1",
		"DOTNET_TC_CallCountingDelayMs":                "0",
		// Each invocation stays in one body, including the initial warmup call.
		"DOTNET_TC_OnStackReplacement": "0",
	})

	type state struct {
		Runtime, Architecture           string
		InitialAddress, PromotedAddress uint64
		PostCalls                       int
	}
	client := &http.Client{Timeout: 45 * time.Second}
	workload := func(path string) state {
		t.Helper()
		resp, err := client.Post(appURL+path, "application/json", nil)
		require.NoError(t, err)
		defer resp.Body.Close()
		require.Equal(t, http.StatusOK, resp.StatusCode)
		var s state
		require.NoError(t, json.NewDecoder(resp.Body).Decode(&s))
		t.Logf("%s: runtime=%s arch=%s initial=0x%x promoted=0x%x post_calls=%d",
			path, s.Runtime, s.Architecture, s.InitialAddress, s.PromotedAddress, s.PostCalls)
		require.Equal(t, "Arm64", s.Architecture)
		if s.InitialAddress == 0 || s.PromotedAddress == 0 || s.InitialAddress == s.PromotedAddress {
			t.Fatal("workload did not confirm execution at a different native address")
		}
		return s
	}
	warmup := workload("/tiering/prepare")
	assertProfile := func(phase string) {
		t.Helper()
		selector := profileLabelSelector(labelMatcher{"service_name", appName}, labelMatcher{"tiering_phase", phase})
		var collapsed string
		var queryErr error
		var mu sync.Mutex
		defer func() {
			if t.Failed() {
				mu.Lock()
				defer mu.Unlock()
				t.Logf("phase=%s query_error=%v last_profile:\n%s", phase, queryErr, collapsed)
			}
		}()
		require.Eventually(t, func() bool {
			profile, err := queryProfile(t, pyroscopeURL, selector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
			mu.Lock()
			collapsed, queryErr = profile, err
			mu.Unlock()
			return err == nil && frameContains(profile, "TieredCompilationWorkload", "HotLoop")
		}, time.Minute, time.Second, "missing HotLoop CPU frame in %s profiles", phase)
	}
	// Diagnose initialization/upload failures separately from missing tiered code.
	assertProfile("warmup")
	post := workload("/tiering/run")
	require.Equal(t, warmup.InitialAddress, post.InitialAddress)
	if post.PostCalls == 0 {
		t.Fatal("no post-transition workload executed")
	}
	// Labels are attached on the sampled thread. Earlier warmup/Tier 0 samples
	// cannot satisfy this query even when they arrive in the same upload.
	assertProfile("post-transition")
}
