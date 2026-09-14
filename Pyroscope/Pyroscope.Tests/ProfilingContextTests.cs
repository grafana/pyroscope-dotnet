using NUnit.Framework;

namespace Pyroscope.Tests;

/// <summary>
/// The propagation itself: whether the async scope, labels and span a request set are published
/// to the profiler's per-thread slots on the threads that actually run the request's <c>await</c>
/// continuations, and taken back off those threads afterwards.
///
/// <see cref="FakeProfiler"/> stands in for the native profiler and records what each thread's
/// slots hold, which is exactly what the sampler would read.
/// </summary>
[TestFixture]
public class ProfilingContextTests
{
    private static ProfilingContext NewContext(FakeProfiler profiler, bool stitching = true, bool propagation = true) =>
        new(profiler, stitchingEnabled: stitching, propagationEnabled: propagation);

    private static LabelSet Set(params (string Key, string Value)[] pairs)
    {
        var builder = LabelSet.Empty.BuildUpon();
        foreach (var (key, value) in pairs)
        {
            builder.Add(key, value);
        }
        return builder.Build();
    }

    // ----- labels ---------------------------------------------------------------------

    [Test]
    public void PushLabelsAppliesThemOnTheCallingThread()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        context.PushLabels(Set(("vehicle", "bike")));

        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
    }

    [Test]
    public async Task LabelsSurviveAnAwaitThatResumesOnTheThreadPool()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        using (Scope(context, Set(("vehicle", "bike"))))
        {
            // Task.Delay always completes asynchronously, so this continuation is scheduled on
            // the thread pool: the case where labels used to disappear.
            await Task.Delay(20).ConfigureAwait(false);

            Assert.That(profiler.LabelsHere(),
                Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }),
                "the resuming thread was never told about the labels");
        }
    }

    [Test]
    public async Task LabelsReachAThreadThatNeverEnteredTheScope()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);
        var pushingThread = Environment.CurrentManagedThreadId;

        using (Scope(context, Set(("vehicle", "bike"))))
        {
            // LongRunning runs the body on a thread created for it, so it cannot have picked the
            // labels up by being reused. If they are there, the ExecutionContext carried them.
            var (workerThread, labels) = await Task.Factory.StartNew(
                () => (Environment.CurrentManagedThreadId, profiler.LabelsHere()),
                CancellationToken.None,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default).ConfigureAwait(false);

            Assert.That(workerThread, Is.Not.EqualTo(pushingThread));
            Assert.That(labels, Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
        }
    }

    [Test]
    public async Task LabelsAreRemovedFromAThreadWhenTheFlowLeavesIt()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);
        var workerThread = 0;

        await RunLabelled().ConfigureAwait(false);

        // The pool will hand that thread to unrelated work next; if it still advertised these
        // labels, that work would be attributed to this request.
        Assert.That(profiler.LabelsOnThread(workerThread), Is.Empty, "thread kept the labels after the flow ended");

        async Task RunLabelled()
        {
            using (Scope(context, Set(("vehicle", "bike"))))
            {
                await Task.Run(() =>
                {
                    workerThread = Environment.CurrentManagedThreadId;
                    Assert.That(profiler.LabelsHere(), Is.Not.Empty);
                }).ConfigureAwait(false);
            }
        }
    }

    [Test]
    public async Task ConcurrentFlowsDoNotSeeEachOthersLabels()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var observed = await Task.WhenAll(Enumerable.Range(0, 16).Select(i => RunRequest($"v{i}"))).ConfigureAwait(false);

        Assert.That(observed, Is.EqualTo(Enumerable.Range(0, 16).Select(i => $"v{i}").ToArray()));

        async Task<string?> RunRequest(string vehicle)
        {
            using (Scope(context, Set(("vehicle", vehicle))))
            {
                await Task.Yield();
                await Task.Delay(Random.Shared.Next(5, 25)).ConfigureAwait(false);
                return profiler.LabelsHere().TryGetValue("vehicle", out var seen) ? seen : null;
            }
        }
    }

    [Test]
    public void NestedLabelSetsRestoreTheOuterSetOnDispose()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        using (Scope(context, Set(("vehicle", "car"))))
        {
            using (Scope(context, Set(("vehicle", "car"), ("driver_region", "eu-north"))))
            {
                Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string>
                {
                    ["vehicle"] = "car",
                    ["driver_region"] = "eu-north",
                }));
            }

            // The inner set's extra key must not linger, or the rest of the request would be
            // labelled with a dimension it is no longer inside.
            Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "car" }));
        }

        Assert.That(profiler.LabelsHere(), Is.Empty);
    }

    [Test]
    public void LabelSetsWithTheSameContentAreInternedOnce()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        for (var i = 0; i < 10; i++)
        {
            // A fresh LabelSet per request, which is the usual pattern.
            using (Scope(context, Set(("vehicle", "bike"))))
            {
            }
        }

        Assert.That(profiler.InternedTagSetCount, Is.EqualTo(1));
    }

    [Test]
    public void KeyOrderDoesNotCreateASecondInternedSet()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        using (Scope(context, Set(("a", "1"), ("b", "2"))))
        {
        }

        using (Scope(context, Set(("b", "2"), ("a", "1"))))
        {
        }

        Assert.That(profiler.InternedTagSetCount, Is.EqualTo(1));
    }

    [Test]
    public async Task ASetThatCanNeverBeInternedIsStillAppliedOnTheThreadThatSetIt()
    {
        // What an application past the profiler's tag-set cap gets: labels cover the synchronous
        // part of the flow, exactly as they did before propagation existed, rather than vanishing.
        var profiler = new FakeProfiler { InternTagSetResult = ProfilingContextSink.NeverInterned };
        var context = NewContext(profiler);

        using (Scope(context, Set(("vehicle", "bike"))))
        {
            Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));

            await Task.Delay(20).ConfigureAwait(false);

            Assert.That(profiler.LabelsHere(), Is.Empty, "a set that cannot be interned must not propagate");
        }
    }

    [Test]
    public void ANonInternableSetReplacesRatherThanMergesWithTheOneBefore()
    {
        var profiler = new FakeProfiler { InternTagSetResult = ProfilingContextSink.NeverInterned };
        var context = NewContext(profiler);

        using (Scope(context, Set(("vehicle", "car"), ("driver_region", "eu-north"))))
        {
            using (Scope(context, Set(("vehicle", "bike"))))
            {
                Assert.That(profiler.LabelsHere(),
                    Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
            }
        }
    }

    [Test]
    public void LabelSetsAreNotCachedWhileTheProfilerIsStillAttaching()
    {
        var profiler = new FakeProfiler { InternTagSetResult = 0 };
        var context = NewContext(profiler);

        var labels = Set(("vehicle", "bike"));
        context.PushLabels(labels);

        // A cold start sets labels before the profiler is ready; that must not be remembered as
        // "this set has no id", or the labels would never appear once it is.
        profiler.InternTagSetResult = null;
        context.PushLabels(labels);

        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
    }

    [Test]
    public async Task AnAsyncLambdaBindsToTheTaskReturningDoOverload()
    {
        var ran = false;

        // This is a compile-time assertion as much as a runtime one: if the async lambda bound to
        // the Action overload -- which is what happened before the Task overloads existed -- Do
        // would return void, this would not compile, and at runtime the label scope would have
        // ended at the first await while the work carried on.
        Task returned = LabelsWrapper.Do(Set(("vehicle", "bike")), async () =>
        {
            await Task.Delay(20).ConfigureAwait(false);
            ran = true;
        });

        await returned.ConfigureAwait(false);

        Assert.That(ran, Is.True);
    }

    [Test]
    public async Task DoReturnsTheResultOfTheAwaitedDelegate()
    {
        var result = await LabelsWrapper.Do(Set(("vehicle", "bike")), async () =>
        {
            await Task.Yield();
            return 42;
        }).ConfigureAwait(false);

        Assert.That(result, Is.EqualTo(42));
    }

    [Test]
    public void WithPropagationOffLabelsOnlyReachTheThreadThatSetThem()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler, propagation: false);

        var pushed = Set(("vehicle", "bike"));
        var previous = context.PushLabels(pushed);

        // Still applied here -- turning propagation off must not turn labels off.
        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));

        context.RestoreLabels(pushed, previous);
        Assert.That(profiler.LabelsHere(), Is.Empty);
    }

    // ----- span context ---------------------------------------------------------------

    [Test]
    public async Task SpanContextSurvivesAnAwait()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        context.PushSpan(new SpanContext(0x1122334455667788, 0xaabb, 0xccdd));

        await Task.Delay(20).ConfigureAwait(false);

        Assert.That(profiler.SpanOnThread(Environment.CurrentManagedThreadId).LocalRootSpanId,
            Is.EqualTo(0x1122334455667788UL),
            "the resuming thread was never told which span it is running");
    }

    [Test]
    public async Task SpanContextIsRemovedFromAThreadWhenTheFlowLeavesIt()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);
        var workerThread = 0;

        await RunSpanned().ConfigureAwait(false);

        Assert.That(profiler.SpanOnThread(workerThread).IsZero, Is.True, "thread kept the span after the flow ended");

        async Task RunSpanned()
        {
            context.PushSpan(new SpanContext(42, 0, 0));
            await Task.Run(() =>
            {
                workerThread = Environment.CurrentManagedThreadId;
                Assert.That(profiler.SpanOnThread(workerThread).LocalRootSpanId, Is.EqualTo(42UL));
            }).ConfigureAwait(false);
        }
    }

    [Test]
    public void WithPropagationOffSpanContextIsStillWrittenToTheCallingThread()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler, propagation: false);

        context.PushSpan(new SpanContext(42, 0, 0));

        Assert.That(profiler.SpanOnThread(Environment.CurrentManagedThreadId).LocalRootSpanId, Is.EqualTo(42UL));
    }

    // ----- scopes and labels together --------------------------------------------------

    [Test]
    public async Task ScopeAndLabelsPropagateTogether()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (scope, previousScope) = context.PushScope("GET /folders");
        var labels = Set(("vehicle", "bike"));
        var previousLabels = context.PushLabels(labels);

        await Task.Delay(20).ConfigureAwait(false);

        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.EqualTo(scope!.NativeId));
        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));

        context.RestoreLabels(labels, previousLabels);
        context.RestoreScope(scope, previousScope);
    }

    [Test]
    public void RepublishingAnUnchangedContextDoesNotTouchTheThread()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);
        var labels = Set(("vehicle", "bike"));

        context.PushScope("GET /folders");
        context.PushLabels(labels);

        var before = profiler.PublishCount;
        context.PushLabels(labels);

        // Three notifying AsyncLocals fire on every ExecutionContext switch. The per-thread cache
        // is what stops that from costing three writes: nothing changed, so nothing is written.
        Assert.That(profiler.PublishCount - before, Is.Zero);
    }

    [Test]
    public void ClosingAScopeOutOfOrderDoesNotDropTheOthers()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        // OpenTelemetry controls when a span ends, so a span opened before a label scope can very
        // well end while that label scope is still open. The three pieces of context are held in
        // one immutable snapshot and restored field by field precisely so that this cannot drop
        // the labels -- which restoring a whole previous value would.
        var (scope, previousScope) = context.PushScope("GET /folders");
        var labels = Set(("vehicle", "bike"));
        context.PushLabels(labels);

        context.RestoreScope(scope, previousScope);

        Assert.That(context.CurrentScope, Is.Null);
        Assert.That(context.CurrentLabels, Is.SameAs(labels));
        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.Zero);
    }

    [Test]
    public void ClosingASpanOutOfOrderDoesNotDropTheLabels()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var previousSpan = context.PushSpan(new SpanContext(42, 0, 0));
        var labels = Set(("vehicle", "bike"));
        context.PushLabels(labels);

        context.RestoreSpan(previousSpan);

        Assert.That(context.CurrentSpan.IsZero, Is.True);
        Assert.That(profiler.LabelsHere(), Is.EquivalentTo(new Dictionary<string, string> { ["vehicle"] = "bike" }));
    }

    // ----- helpers ---------------------------------------------------------------------

    private static IDisposable Scope(ProfilingContext context, LabelSet labels)
    {
        var previous = context.PushLabels(labels);
        return new Restorer(() => context.RestoreLabels(labels, previous));
    }

    private sealed class Restorer : IDisposable
    {
        private readonly Action _dispose;

        public Restorer(Action dispose) => _dispose = dispose;

        public void Dispose() => _dispose();
    }
}
