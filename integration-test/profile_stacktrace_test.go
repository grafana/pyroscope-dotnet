package integrationtest

import (
	"context"
	"fmt"
	"net/http"
	"regexp"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"

	"github.com/stretchr/testify/assert"
)

// stackContainsChain checks that at least one stack in the collapsed output
// contains the given frames in caller→callee order.  Each entry is
// [className, methodName] using the same matching rules as frameContains.
func stackContainsChain(collapsed string, chain [][2]string) bool {
	patterns := make([]*regexp.Regexp, len(chain))
	for i, f := range chain {
		patterns[i] = regexp.MustCompile(
			`(?:^|!)` + regexp.QuoteMeta(f[0]) + `(?:<[^>]*>)?\.` + regexp.QuoteMeta(f[1]) + `(?:<|;|\s|$)`)
	}
	for _, line := range strings.Split(collapsed, "\n") {
		frames := strings.Split(line, ";")
		pi := 0
		for _, frame := range frames {
			if pi >= len(patterns) {
				break
			}
			if patterns[pi].MatchString(frame) {
				pi++
			}
		}
		if pi == len(patterns) {
			return true
		}
	}
	return false
}

func runAllocLoadGenerator(ctx context.Context, t *testing.T, appBaseURL string) {
	t.Helper()
	go func() {
		client := &http.Client{Timeout: 60 * time.Second}
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}
			resp, err := client.Get(appBaseURL + "/alloc")
			if err == nil {
				_ = resp.Body.Close()
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(200 * time.Millisecond):
			}
		}
	}()
}

func runNPELoadGenerator(ctx context.Context, t *testing.T, appBaseURL string) {
	t.Helper()
	go func() {
		client := &http.Client{Timeout: 60 * time.Second}
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}
			resp, err := client.Get(appBaseURL + "/npe")
			if err == nil {
				_ = resp.Body.Close()
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(500 * time.Millisecond):
			}
		}
	}()
}

func TestAllocationProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.alloc-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
	})
	runAllocLoadGenerator(ctx, t, appBaseURL)

	labelSelector := fmt.Sprintf(`{service_name="%s"}`, appName)
	expectedChain := [][2]string{
		{"AllocWork", "Work"},
		{"AllocWork", "Allocate"},
	}
	var lastCollapsed string
	var lastErr error
	ok := assert.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "alloc:alloc_samples:count:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, 3*time.Minute, 5*time.Second)

	if !ok {
		t.Logf("label selector: %s", labelSelector)
		t.Logf("last profile query error: %v", lastErr)
		t.Logf("last collapsed profile:\n%s", lastCollapsed)
		t.FailNow()
	}
	t.Logf("collapsed allocation profile:\n%s", lastCollapsed)
}

func TestExceptionProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.exception-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":            appName,
		"PYROSCOPE_PROFILING_ENABLED":           "true",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED": "true",
	})
	runNPELoadGenerator(ctx, t, appBaseURL)

	labelSelector := fmt.Sprintf(`{service_name="%s"}`, appName)
	expectedChain := [][2]string{
		{"EndpointMiddleware", "Invoke"},
		{"NPE", "Work"},
	}
	var lastCollapsed string
	var lastErr error
	ok := assert.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "exception:exception:count:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, 3*time.Minute, 5*time.Second)

	if !ok {
		t.Logf("label selector: %s", labelSelector)
		t.Logf("last profile query error: %v", lastErr)
		t.Logf("last collapsed profile:\n%s", lastCollapsed)
		t.FailNow()
	}
	t.Logf("collapsed exception profile:\n%s", lastCollapsed)
}

func TestLockProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.lock-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED": "true",
	})
	for i := 0; i < 3; i++ {
		runLoadGenerator(ctx, t, appBaseURL)
	}

	labelSelector := fmt.Sprintf(`{service_name="%s"}`, appName)
	expectedChain := [][2]string{
		{"EndpointMiddleware", "Invoke"},
		{"OrderService", "FindNearestVehicle"},
	}
	var lastCollapsed string
	var lastErr error
	ok := assert.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "lock:lock_count:count:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, 3*time.Minute, 5*time.Second)

	if !ok {
		t.Logf("label selector: %s", labelSelector)
		t.Logf("last profile query error: %v", lastErr)
		t.Logf("last collapsed profile:\n%s", lastCollapsed)
		t.FailNow()
	}
	t.Logf("collapsed lock profile:\n%s", lastCollapsed)
}
