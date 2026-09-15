package integrationtest

import (
	"context"
	"fmt"
	"regexp"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/require"
)

// Frames the async/await machinery puts on every continuation's stack. They hold almost
// no self time but a third of all frames, and they come and go with what the JIT inlined,
// which is what splits one logical path across several flamegraph nodes.
var plumbingFramePatterns = map[string]*regexp.Regexp{
	"continuation box":     regexp.MustCompile(`AsyncStateMachineBox`),
	"thread-pool dispatch": regexp.MustCompile(`ThreadPoolWorkQueue\.Dispatch`),
	"execution context":    regexp.MustCompile(`ExecutionContext\.Run`),
	"builder start":        regexp.MustCompile(`Async(Task)?MethodBuilder(Core)?\.Start`),
	"completion unwind":    regexp.MustCompile(`Task\.RunContinuations|UnwrapPromise`),
}

// The compiler's state machine decoration: "Class.<Method>d__4.MoveNext". Cleanup renames
// these to "Class.Method" so they merge with the kickoff frame the compiler emits too.
var stateMachineDecorationPattern = regexp.MustCompile(`>d__\d+`)

// framesMatching returns the distinct frames in a collapsed profile that match pattern.
func framesMatching(collapsed string, pattern *regexp.Regexp) []string {
	seen := make(map[string]bool)
	var matching []string
	for _, frames := range collapsedStacks(collapsed) {
		for _, frame := range frames {
			if pattern.MatchString(frame) && !seen[frame] {
				seen[frame] = true
				matching = append(matching, frame)
			}
		}
	}
	return matching
}

func examples(frames []string, n int) string {
	if len(frames) > n {
		frames = frames[:n]
	}
	return strings.Join(frames, "\n  ")
}

// TestAsyncFrameCleanup is the end-to-end check that the async machinery is gone from the
// reported stacks. /async-order awaits and then burns CPU, so every sample of that CPU work
// physically sits under a thread-pool continuation -- the exact shape that carries the
// machinery frames.
func TestAsyncFrameCleanup(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-frame-cleanup.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, nil)

	runAsyncOrderLoadGenerator(ctx, t, appBaseURL)

	labelSelector := profileLabelSelector(labelMatcher{"service_name", appName})

	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelSelector, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		// Wait for the post-await work itself: without it there is no continuation stack
		// to have cleaned up, and the assertions below would pass vacuously.
		return len(stacksContainingMethod(lastCollapsed, afterAwaitFrame)) > 0
	}, asyncStitchingWait, 5*time.Second,
		"expected the post-await work to be profiled\n%s",
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	for name, pattern := range plumbingFramePatterns {
		if found := framesMatching(lastCollapsed, pattern); len(found) > 0 {
			t.Errorf("%d %s frames survived cleanup:\n  %s", len(found), name, examples(found, 3))
		}
	}

	if found := framesMatching(lastCollapsed, stateMachineDecorationPattern); len(found) > 0 {
		t.Errorf("%d frames still carry the state machine decoration, so they cannot merge "+
			"with their kickoff frame:\n  %s", len(found), examples(found, 3))
	}
}

// TestAsyncFrameCleanupDisabled is the "before" half: with cleanup off the machinery is
// reported exactly as it was, which is also what proves the assertions above are testing
// something real rather than frames the runtime never produced.
func TestAsyncFrameCleanupDisabled(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-frame-cleanup-off.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, map[string]string{
		"PYROSCOPE_ASYNC_FRAME_CLEANUP_ENABLED": "false",
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
		return len(framesMatching(lastCollapsed, plumbingFramePatterns["continuation box"])) > 0
	}, asyncStitchingWait, 5*time.Second,
		"expected the continuation box frames to be reported with cleanup disabled\n%s",
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	boxes := framesMatching(lastCollapsed, plumbingFramePatterns["continuation box"])
	t.Logf("cleanup off: %d distinct continuation box frames, e.g.\n  %s", len(boxes), examples(boxes, 3))

	// The decorated state machine names come back too, so the rename is switched off end
	// to end rather than only the dropping half.
	decorated := framesMatching(lastCollapsed, stateMachineDecorationPattern)
	if len(decorated) == 0 {
		t.Errorf("expected state-machine-decorated frames with cleanup disabled, found none")
	}
	t.Logf("cleanup off: %d distinct decorated frames, e.g.\n  %s", len(decorated), examples(decorated, 3))
}
