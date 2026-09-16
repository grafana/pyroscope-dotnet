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
	"pyroscope-dotnet-integration-test/require"
)

// asyncScopeFrame is the scope the /async-order endpoint opens; see
// IntegrationTest/AsyncOrderService.cs.
const asyncScopeFrame = "GET /async-order"

const (
	// Frame that runs on a thread created for the awaited task, so it can only be
	// attributed to the endpoint via the ExecutionContext.
	dedicatedThreadFrame = "SpinOnDedicatedThread"
	// Frame that runs in a thread-pool continuation after `await Task.Delay`.
	afterAwaitFrame = "SpinAfterAwait"
	// A synchronous endpoint that never opens a scope; its frames must not be pulled
	// under the async scope.
	unscopedFrame = "FindNearestVehicle"
)

// asyncStitchingWait is generous: the profiler batches samples and uploads on
// DD_PROFILING_UPLOAD_PERIOD, and the load generator has to drive enough requests for
// the CPU burn to be sampled.
const asyncStitchingWait = 3 * time.Minute

func runAsyncOrderLoadGenerator(ctx context.Context, t *testing.T, appBaseURL string) {
	t.Helper()
	go func() {
		client := &http.Client{Timeout: 60 * time.Second}
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}
			resp, err := client.Get(appBaseURL + "/async-order")
			if err == nil {
				_ = resp.Body.Close()
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(100 * time.Millisecond):
			}
		}
	}()
}

// collapsedStacks splits the collapsed profile into per-stack frame lists. Frames are
// root-first, so a caller precedes its callees.
func collapsedStacks(collapsed string) [][]string {
	var stacks [][]string
	for _, line := range strings.Split(collapsed, "\n") {
		if frames := collapsedStackFrames(line); len(frames) > 0 {
			stacks = append(stacks, frames)
		}
	}
	return stacks
}

// methodFramePattern matches a frame by method name, whatever namespace/class/state
// machine decoration the profiler put around it.
func methodFramePattern(method string) *regexp.Regexp {
	return regexp.MustCompile(`(?:^|[!.<])` + regexp.QuoteMeta(method) + `(?:>|\(|<|$)`)
}

func stacksContainingMethod(collapsed, method string) [][]string {
	pattern := methodFramePattern(method)
	var matching [][]string
	for _, frames := range collapsedStacks(collapsed) {
		for _, frame := range frames {
			if pattern.MatchString(frame) {
				matching = append(matching, frames)
				break
			}
		}
	}
	return matching
}

func hasScopeFrame(frames []string, scope string) bool {
	for _, frame := range frames {
		if strings.TrimSpace(frame) == scope {
			return true
		}
	}
	return false
}

// scopeReport summarises how the stacks containing a method are attributed.
type scopeReport struct {
	// Stacks that do have the scope as an ancestor.
	Rooted int
	// Stacks that do not: in a stitched profile there are none, which is what makes
	// inclusive time per endpoint meaningful again.
	Unrooted int
	// Up to three of each, for the failure message.
	RootedExamples   []string
	UnrootedExamples []string
}

func reportScopeAttribution(collapsed, method, scope string) scopeReport {
	var report scopeReport
	for _, frames := range stacksContainingMethod(collapsed, method) {
		stack := strings.Join(frames, ";")
		if hasScopeFrame(frames, scope) {
			report.Rooted++
			if len(report.RootedExamples) < 3 {
				report.RootedExamples = append(report.RootedExamples, stack)
			}
			continue
		}
		report.Unrooted++
		if len(report.UnrootedExamples) < 3 {
			report.UnrootedExamples = append(report.UnrootedExamples, stack)
		}
	}
	return report
}

func startAsyncStitchingApp(t *testing.T, appName string, extraEnv map[string]string) (pyroscopeURL, appBaseURL string) {
	t.Helper()
	net := dockertest.CreateNetwork(t)
	pyroscopeURL = startPyroscope(t, net)

	// The async features are opt-in, so every test that exercises one has to ask for it. All
	// three are enabled here and the "disabled" tests switch off the single one they are about,
	// which keeps each of those a one-variable difference from the enabled case.
	env := map[string]string{
		"PYROSCOPE_APPLICATION_NAME":                  appName,
		"PYROSCOPE_PROFILING_ENABLED":                 "true",
		"PYROSCOPE_PROFILING_CPU_ENABLED":             "true",
		"PYROSCOPE_PROFILING_WALLTIME_ENABLED":        "true",
		"PYROSCOPE_ASYNC_STITCHING_ENABLED":           "true",
		"PYROSCOPE_ASYNC_CONTEXT_PROPAGATION_ENABLED": "true",
		"PYROSCOPE_ASYNC_FRAME_CLEANUP_ENABLED":       "true",
		"DD_PROFILING_UPLOAD_PERIOD":                  "10",
	}
	for k, v := range extraEnv {
		env[k] = v
	}
	appBaseURL = startAppWithEnv(t, net, pyroscopeURL, envLibcType(), envDotnetVersion(), false, env)
	return pyroscopeURL, appBaseURL
}

// TestAsyncStackStitching is the end-to-end check that an `await` no longer detaches work
// from the endpoint that caused it.
//
// /async-order opens a Pyroscope async scope, awaits, and then burns CPU. The physical
// stack of that CPU work is rooted at the thread pool and does not mention the endpoint;
// if the profiler still reports the endpoint above it, the scope survived the await.
func TestAsyncStackStitching(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-stitching.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, nil)

	runAsyncOrderLoadGenerator(ctx, t, appBaseURL)
	// Drive an unscoped synchronous endpoint at the same time: its frames share the
	// thread pool with the scoped work and must not be swept under the scope.
	runLoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})

	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		// Both post-await frames must be present and rooted at the scope before we start
		// asserting on the details.
		for _, method := range []string{dedicatedThreadFrame, afterAwaitFrame} {
			if reportScopeAttribution(lastCollapsed, method, asyncScopeFrame).Rooted == 0 {
				return false
			}
		}
		return true
	}, asyncStitchingWait, 5*time.Second,
		"expected post-await work to be rooted at %q\n%s",
		asyncScopeFrame,
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	// Every sample of the awaited work belongs to the endpoint: this is the property that
	// makes inclusive time per endpoint meaningful again.
	for _, method := range []string{dedicatedThreadFrame, afterAwaitFrame} {
		report := reportScopeAttribution(lastCollapsed, method, asyncScopeFrame)
		t.Logf("%s: %d stacks rooted at %q, %d not", method, report.Rooted, asyncScopeFrame, report.Unrooted)
		// The reconstructed tree, for the record: the scope, then the thread-pool root the
		// continuation physically ran on, then the work.
		for _, stack := range report.RootedExamples {
			t.Logf("  stitched: %s", stack)
		}
		if report.Unrooted != 0 {
			t.Errorf("%d stacks containing %s are not rooted at %q:\n%s",
				report.Unrooted, method, asyncScopeFrame, strings.Join(report.UnrootedExamples, "\n"))
		}
	}

	// The scope must not bleed onto threads running unrelated work. Without the clearing
	// half of the mechanism, a pool thread would keep the scope after the continuation
	// finished and mislabel whatever it picked up next.
	if report := reportScopeAttribution(lastCollapsed, unscopedFrame, asyncScopeFrame); report.Rooted != 0 {
		t.Errorf("%d of %d stacks of the unscoped endpoint (%s) were attributed to %q:\n%s",
			report.Rooted, report.Rooted+report.Unrooted, unscopedFrame, asyncScopeFrame,
			strings.Join(report.RootedExamples, "\n"))
	}
}

// TestAsyncStackStitchingDisabled is the "before" half of the comparison: with stitching
// off the profiler behaves exactly as it did before this feature -- the post-await work is
// still profiled, but nothing ties it to the endpoint.
func TestAsyncStackStitchingDisabled(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-stitching-off.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, map[string]string{
		"PYROSCOPE_ASYNC_STITCHING_ENABLED": "false",
	})

	runAsyncOrderLoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})

	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return len(stacksContainingMethod(lastCollapsed, dedicatedThreadFrame)) > 0
	}, asyncStitchingWait, 5*time.Second,
		"expected the post-await work to still be profiled with stitching disabled\n%s",
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	for _, method := range []string{dedicatedThreadFrame, afterAwaitFrame} {
		report := reportScopeAttribution(lastCollapsed, method, asyncScopeFrame)
		t.Logf("%s: %d stacks rooted at %q, %d not", method, report.Rooted, asyncScopeFrame, report.Unrooted)
		// The unstitched shape, for comparison with the test above: rooted at the thread
		// pool, with nothing tying it to the endpoint.
		for _, stack := range report.UnrootedExamples {
			t.Logf("  unstitched: %s", stack)
		}
		if report.Rooted != 0 {
			t.Errorf("stitching is disabled but %d stacks containing %s are rooted at %q",
				report.Rooted, method, asyncScopeFrame)
		}
	}

	// No scope frame anywhere: the switch turns the feature off end to end, it does not
	// merely stop new scopes from being pushed.
	for _, frames := range collapsedStacks(lastCollapsed) {
		if hasScopeFrame(frames, asyncScopeFrame) {
			t.Fatalf("found scope frame %q while stitching is disabled: %s", asyncScopeFrame, strings.Join(frames, ";"))
		}
	}
}
