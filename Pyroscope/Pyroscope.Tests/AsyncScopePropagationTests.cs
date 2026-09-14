using NUnit.Framework;

namespace Pyroscope.Tests;

/// <summary>
/// Async stack stitching's half of the propagation: whether the logical scope a request opened
/// reaches the threads that run its <c>await</c> continuations, and is taken back off them.
/// </summary>
[TestFixture]
public class AsyncScopePropagationTests
{
    private const uint NoScope = 0;

    private static ProfilingContext NewContext(FakeProfiler profiler, bool stitching = true) =>
        new(profiler, stitchingEnabled: stitching, propagationEnabled: true);

    [Test]
    public void PushPublishesTheScopeOnTheCallingThread()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (pushed, previous) = context.PushScope("GET /folders");

        Assert.That(pushed, Is.Not.Null);
        Assert.That(previous, Is.Null);
        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.EqualTo(pushed!.NativeId));
        Assert.That(context.CurrentScope!.Name, Is.EqualTo("GET /folders"));
    }

    [Test]
    public void RestoreTakesTheScopeBackOff()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (pushed, previous) = context.PushScope("GET /folders");
        context.RestoreScope(pushed, previous);

        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.EqualTo(NoScope));
        Assert.That(context.CurrentScope, Is.Null);
    }

    [Test]
    public void NestedScopesChainOntoTheirParent()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (endpoint, _) = context.PushScope("GET /folders");
        var (service, previousService) = context.PushScope("FolderService.Load");

        Assert.That(service!.Parent, Is.SameAs(endpoint));
        Assert.That(service.Depth, Is.EqualTo(2));

        context.RestoreScope(service, previousService);
        Assert.That(context.CurrentScope, Is.SameAs(endpoint));
    }

    [Test]
    public async Task ScopeSurvivesAnAwaitThatResumesOnTheThreadPool()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (pushed, previous) = context.PushScope("GET /folders");

        // Task.Delay always completes asynchronously, so this continuation is scheduled on the
        // thread pool: exactly the case where the physical stack loses the caller.
        await Task.Delay(20).ConfigureAwait(false);

        Assert.That(context.CurrentScope, Is.SameAs(pushed), "the scope did not survive the await");
        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.EqualTo(pushed!.NativeId),
            "the resuming thread was never told which scope it is running");

        context.RestoreScope(pushed, previous);
    }

    [Test]
    public async Task ScopeReachesAThreadThatNeverEnteredIt()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (pushed, previous) = context.PushScope("GET /folders");
        var pushingThread = Environment.CurrentManagedThreadId;

        // LongRunning runs the body on a thread created for it, so it cannot have inherited the
        // scope by being reused. If the profiler's slot on that thread names the scope, the
        // ExecutionContext carried it.
        var (workerThread, observed) = await Task.Factory.StartNew(
            () => (Environment.CurrentManagedThreadId, profiler.ScopeOnThread(Environment.CurrentManagedThreadId)),
            CancellationToken.None,
            TaskCreationOptions.LongRunning,
            TaskScheduler.Default).ConfigureAwait(false);

        Assert.That(workerThread, Is.Not.EqualTo(pushingThread));
        Assert.That(observed, Is.EqualTo(pushed!.NativeId));

        context.RestoreScope(pushed, previous);
    }

    [Test]
    public async Task ScopeIsRemovedFromAThreadWhenTheFlowLeavesIt()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);
        var workerThread = 0;

        await RunScoped().ConfigureAwait(false);

        // The pool will hand that thread to unrelated work next; if it still advertised the
        // scope, that work would be re-rooted under this request.
        Assert.That(profiler.ScopeOnThread(workerThread), Is.EqualTo(NoScope), "thread kept the scope after the flow ended");

        async Task RunScoped()
        {
            var (pushed, previous) = context.PushScope("GET /folders");
            await Task.Run(() =>
            {
                workerThread = Environment.CurrentManagedThreadId;
                Assert.That(profiler.ScopeOnThread(workerThread), Is.EqualTo(pushed!.NativeId));
            }).ConfigureAwait(false);
            context.RestoreScope(pushed, previous);
        }
    }

    [Test]
    public async Task ConcurrentFlowsDoNotSeeEachOthersScopes()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var observed = await Task.WhenAll(Enumerable.Range(0, 16).Select(i => RunRequest($"GET /r{i}")))
            .ConfigureAwait(false);

        Assert.That(observed, Is.EqualTo(Enumerable.Range(0, 16).Select(i => $"GET /r{i}").ToArray()));

        async Task<string?> RunRequest(string name)
        {
            var (pushed, previous) = context.PushScope(name);
            await Task.Yield();
            await Task.Delay(Random.Shared.Next(5, 25)).ConfigureAwait(false);
            var seen = context.CurrentScope?.Name;
            context.RestoreScope(pushed, previous);
            return seen;
        }
    }

    [Test]
    public async Task AnInnerScopeDoesNotEscapeTheFlowThatOpenedIt()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (endpoint, previousEndpoint) = context.PushScope("GET /folders");

        // Scopes opened inside a branch belong to that branch: the branch runs on its own
        // ExecutionContext copy, so its scope must not leak back into the caller.
        await Task.Run(async () =>
        {
            var (inner, previousInner) = context.PushScope("DbCommand");
            await Task.Yield();
            Assert.That(context.CurrentScope, Is.SameAs(inner));
            context.RestoreScope(inner, previousInner);
        }).ConfigureAwait(false);

        Assert.That(context.CurrentScope, Is.SameAs(endpoint));

        context.RestoreScope(endpoint, previousEndpoint);
    }

    [Test]
    public async Task RestoringFromAnUnrelatedFlowDoesNothing()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        // A span can be ended from a callback running in a different async flow than the one that
        // started it. Restoring there would attribute the rest of *that* flow to a scope it never
        // entered, so it must be a no-op.
        var (foreign, foreignPrevious) = await Task.Run(() => context.PushScope("GET /elsewhere")).ConfigureAwait(false);

        var (mine, minePrevious) = context.PushScope("GET /folders");
        context.RestoreScope(foreign, foreignPrevious);

        Assert.That(context.CurrentScope, Is.SameAs(mine));

        context.RestoreScope(mine, minePrevious);
    }

    [Test]
    public void RestoreIsIdempotent()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        var (pushed, previous) = context.PushScope("GET /folders");
        context.RestoreScope(pushed, previous);
        var publishesAfterFirstRestore = profiler.PublishCount;
        context.RestoreScope(pushed, previous);

        Assert.That(profiler.PublishCount, Is.EqualTo(publishesAfterFirstRestore));
        Assert.That(context.CurrentScope, Is.Null);
    }

    [Test]
    public void ScopesAreInternedOncePerDistinctPath()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        for (var i = 0; i < 5; i++)
        {
            var (pushed, previous) = context.PushScope("GET /folders");
            context.RestoreScope(pushed, previous);
        }

        // Five requests to the same endpoint must not cost five native interning calls; they all
        // resolve to the id the first one got.
        Assert.That(profiler.ScopeOnThread(Environment.CurrentManagedThreadId), Is.EqualTo(NoScope));
        Assert.That(context.PushScope("GET /folders").Pushed!.NativeId, Is.EqualTo(1u));
    }

    [Test]
    public void WithStitchingOffNothingIsPushed()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler, stitching: false);

        var (pushed, previous) = context.PushScope("GET /folders");
        context.RestoreScope(pushed, previous);

        Assert.That(pushed, Is.Null);
        Assert.That(previous, Is.Null);
        Assert.That(context.CurrentScope, Is.Null);
    }

    [Test]
    public void AnEmptyNamePushesNothing()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        Assert.That(context.PushScope("").Pushed, Is.Null);
        Assert.That(context.PushScope(null).Pushed, Is.Null);
    }

    [Test]
    public void ChainsStopDeepeningAtTheDepthCap()
    {
        var profiler = new FakeProfiler();
        var context = NewContext(profiler);

        // Matches AsyncScopeStore::MaxDepth on the native side.
        const int maxDepth = 32;
        for (var i = 0; i < maxDepth; i++)
        {
            Assert.That(context.PushScope($"scope{i}").Pushed, Is.Not.Null, $"scope{i} was dropped");
        }

        Assert.That(context.CurrentScope!.Depth, Is.EqualTo(maxDepth));
        Assert.That(context.PushScope("one too many").Pushed, Is.Null);
        // The work stays attributed to the enclosing chain rather than being detached.
        Assert.That(context.CurrentScope!.Depth, Is.EqualTo(maxDepth));
    }

    [Test]
    public void AThrowingSinkDisablesFurtherCallsInsteadOfPropagating()
    {
        // The change handler runs inside the runtime's ExecutionContext switch, so a throwing
        // P/Invoke there would surface in code that never asked to handle it.
        var sink = new ThrowingSink();
        var context = new ProfilingContext(sink, stitchingEnabled: true, propagationEnabled: true);

        Assert.DoesNotThrow(() =>
        {
            var (pushed, previous) = context.PushScope("GET /folders");
            context.RestoreScope(pushed, previous);
            context.PushScope("GET /documents");
            context.PushLabels(LabelSet.Empty.BuildUpon().Add("vehicle", "bike").Build());
        });

        Assert.That(sink.Calls, Is.EqualTo(1), "the sink was called again after it failed");
    }

    private sealed class ThrowingSink : IProfilingContextSink
    {
        public int Calls { get; private set; }

        public uint InternAsyncScope(uint parentScopeId, string name) => Fail();

        public uint InternDynamicTagSet(string[] keys, string[] values) => Fail();

        public void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId) => Fail();

        public void SetDynamicTag(string key, string value) => Fail();

        public void ClearDynamicTags() => Fail();

        public void SetSpanContext(in SpanContext context) => Fail();

        private uint Fail()
        {
            Calls++;
            throw new DllNotFoundException("Pyroscope.Profiler.Native");
        }
    }
}
