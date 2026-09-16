package integrationtest

import (
	"context"
	"fmt"
	"strings"
	"testing"
	"time"

	"pyroscope-dotnet-integration-test/require"
)

// The label the /async-order endpoint attaches to its work; see
// IntegrationTest/AsyncOrderService.cs.
const (
	asyncLabelKey   = "endpoint"
	asyncLabelValue = "async-order"
)

// TestAsyncLabelPropagation is the end-to-end check that dynamic labels no longer stop at the
// first `await`.
//
// Dynamic labels live in a per-OS-thread slot, so labels set by a request used to be invisible to
// the thread-pool threads that run its continuations. /async-order sets a label, awaits, and then
// burns CPU; if the label-filtered profile contains that post-await work, the label followed the
// request across the await.
func TestAsyncLabelPropagation(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-labels.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, nil)

	runAsyncOrderLoadGenerator(ctx, t, appBaseURL)
	// Drive an unlabelled synchronous endpoint at the same time: it shares the thread pool with
	// the labelled work, and its frames must not end up under the label.
	runLoadGenerator(ctx, t, appBaseURL)

	labelled := profileLabelSelector(
		labelMatcher{"service_name", appName},
		labelMatcher{asyncLabelKey, asyncLabelValue},
	)

	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, labelled, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		// The frame that runs on a thread created for the awaited task is the decisive one: that
		// thread cannot have been carrying the label for any other reason.
		return len(stacksContainingMethod(lastCollapsed, dedicatedThreadFrame)) > 0 &&
			len(stacksContainingMethod(lastCollapsed, afterAwaitFrame)) > 0
	}, asyncStitchingWait, 5*time.Second,
		"expected post-await work to carry the %q=%q label\n%s",
		asyncLabelKey, asyncLabelValue,
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	for _, method := range []string{dedicatedThreadFrame, afterAwaitFrame} {
		stacks := stacksContainingMethod(lastCollapsed, method)
		t.Logf("%s: %d labelled stacks", method, len(stacks))
		if len(stacks) > 0 {
			t.Logf("  labelled: %s", strings.Join(stacks[0], ";"))
		}
	}

	// The label must not bleed onto threads running unrelated work. Without the clearing half of
	// the mechanism, a pool thread would keep the label after the continuation finished and
	// mislabel whatever it picked up next.
	if leaked := stacksContainingMethod(lastCollapsed, unscopedFrame); len(leaked) != 0 {
		t.Errorf("%d stacks of the unlabelled endpoint (%s) carry %q=%q:\n%s",
			len(leaked), unscopedFrame, asyncLabelKey, asyncLabelValue, strings.Join(leaked[0], ";"))
	}
}

// TestAsyncLabelPropagationDisabled is the "before" half: with propagation off, a thread the
// request never ran on cannot see the labels at all, while the threads that do see them see them
// by accident. Only the deterministic half is asserted.
func TestAsyncLabelPropagationDisabled(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	appName := fmt.Sprintf("rideshare.async-labels-off.%d", time.Now().UnixNano())
	pyroscopeURL, appBaseURL := startAsyncStitchingApp(t, appName, map[string]string{
		"PYROSCOPE_ASYNC_CONTEXT_PROPAGATION_ENABLED": "false",
	})

	runAsyncOrderLoadGenerator(ctx, t, appBaseURL)

	unlabelled := profileLabelSelector(labelMatcher{"service_name", appName})
	labelled := profileLabelSelector(
		labelMatcher{"service_name", appName},
		labelMatcher{asyncLabelKey, asyncLabelValue},
	)

	// Wait until the endpoint's work has been profiled at all, so the assertion below is not just
	// checking an empty profile.
	var lastCollapsed string
	var lastErr error
	require.Eventually(t, func() bool {
		lastCollapsed, lastErr = queryProfile(t, pyroscopeURL, unlabelled, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
		if lastErr != nil || lastCollapsed == "" {
			return false
		}
		return len(stacksContainingMethod(lastCollapsed, dedicatedThreadFrame)) > 0
	}, asyncStitchingWait, 5*time.Second,
		"expected the post-await work to still be profiled with propagation disabled\n%s",
		profileQueryDebug{collapsed: &lastCollapsed, err: &lastErr})

	labelledCollapsed, err := queryProfile(t, pyroscopeURL, labelled, "process_cpu:cpu:nanoseconds:cpu:nanoseconds")
	if err != nil {
		t.Fatalf("querying the labelled profile: %v", err)
	}

	// The awaited task body runs on a thread created for it, so with propagation off nothing can
	// have put the label there. This is the deterministic half.
	if onDedicated := stacksContainingMethod(labelledCollapsed, dedicatedThreadFrame); len(onDedicated) != 0 {
		t.Errorf("propagation is disabled but %d stacks containing %s carry %q=%q:\n%s",
			len(onDedicated), dedicatedThreadFrame, asyncLabelKey, asyncLabelValue,
			strings.Join(onDedicated[0], ";"))
	}

	// The thread-pool continuation may or may not land on a thread that still has the label stuck
	// to it from before the await, so this is logged rather than asserted on. TestAsyncLabelPropagation
	// covers the clearing that removes it.
	afterAwait := stacksContainingMethod(labelledCollapsed, afterAwaitFrame)
	t.Logf("%s: %d labelled stacks with propagation disabled (stale labels left on pool threads)",
		afterAwaitFrame, len(afterAwait))
}
