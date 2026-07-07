package integrationtest

import (
	"context"
	"fmt"
	"net/http"
	"pyroscope-dotnet-integration-test/require"
	"regexp"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/dockertest"
)

const (
	allocationProfileTypeID = "memory:alloc_samples:count:space:bytes"
	allocationProfileWait   = 2 * time.Minute
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
		frames := collapsedStackFrames(line)
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

func collapsedStackFrames(line string) []string {
	line = strings.TrimSpace(line)
	if line == "" {
		return nil
	}
	if valueStart := strings.LastIndexByte(line, ' '); valueStart >= 0 {
		line = line[:valueStart]
	}
	return strings.Split(line, ";")
}

func stackContainsChainWithLeaf(collapsed string, chain [][2]string, leafTypeName string) bool {
	patterns := make([]*regexp.Regexp, len(chain))
	for i, f := range chain {
		patterns[i] = regexp.MustCompile(
			`(?:^|!)` + regexp.QuoteMeta(f[0]) + `(?:<[^>]*>)?\.` + regexp.QuoteMeta(f[1]) + `(?:<|;|\s|$)`)
	}
	leafPattern := regexp.MustCompile(`(?:^|[!:.])` + regexp.QuoteMeta(leafTypeName) + `(?:<[^>]*>)?$`)
	for _, line := range strings.Split(collapsed, "\n") {
		frames := collapsedStackFrames(line)
		if len(frames) == 0 || !leafPattern.MatchString(frames[len(frames)-1]) {
			continue
		}
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

type profileQueryDebug struct {
	collapsed *string
	err       *error
}

func (d profileQueryDebug) String() string {
	collapsed := ""
	if d.collapsed != nil {
		collapsed = *d.collapsed
	}
	if d.err != nil && *d.err != nil {
		return fmt.Sprintf("last error: %v\nlast collapsed:\n%s", *d.err, collapsed)
	}
	return fmt.Sprintf("last collapsed:\n%s", collapsed)
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
	appBaseURL := startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
		"DD_PROFILING_UPLOAD_PERIOD":             "10",
	})
	runAllocLoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})
	expectedChain := [][2]string{
		{"AllocWork", "Work"},
		{"AllocWork", "Allocate"},
	}
	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, allocationProfileTypeID)
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, allocationProfileWait, 5*time.Second)
}

func TestAllocationTypeLeafProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.alloc-leaf-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_ALLOCATION_ENABLED": "true",
		"PYROSCOPE_ALLOCATION_TYPE_LEAF_ENABLED": "true",
		"DD_PROFILING_UPLOAD_PERIOD":             "10",
	})
	runAllocLoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})
	expectedChain := [][2]string{
		{"AllocWork", "Work"},
		{"AllocWork", "Allocate"},
	}
	expectedLeafType := "cls:AllocatedLeafPayload"
	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, allocationProfileTypeID)
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChainWithLeaf(lastCollapsed, expectedChain, expectedLeafType)
	}, allocationProfileWait, 5*time.Second,
		"expected allocation profile stack ending in %q\n%s",
		expectedLeafType,
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})
}

func TestExceptionProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.exception-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":            appName,
		"PYROSCOPE_PROFILING_ENABLED":           "true",
		"PYROSCOPE_PROFILING_EXCEPTION_ENABLED": "true",
	})
	runNPELoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})
	expectedChain := [][2]string{
		{"EndpointMiddleware", "Invoke"},
		{"NPE", "Work"},
	}
	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "exception:exception:count:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, 3*time.Minute, 5*time.Second)
}

func TestLockProfileStacktraces(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	net := dockertest.CreateNetwork(t)
	pyroscopeURL := startPyroscope(t, net)
	appName := fmt.Sprintf("rideshare.lock-test.%d", time.Now().UnixNano())
	appBaseURL := startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, map[string]string{
		"PYROSCOPE_APPLICATION_NAME":             appName,
		"PYROSCOPE_PROFILING_ENABLED":            "true",
		"PYROSCOPE_PROFILING_CONTENTION_ENABLED": "true",
	})
	for i := 0; i < 3; i++ {
		runLoadGenerator(ctx, t, appBaseURL)
	}

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})
	expectedChain := [][2]string{
		{"EndpointMiddleware", "Invoke"},
		{"OrderService", "FindNearestVehicle"},
	}
	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "lock:lock_count:count:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return stackContainsChain(lastCollapsed, expectedChain)
	}, 3*time.Minute, 5*time.Second)
}
