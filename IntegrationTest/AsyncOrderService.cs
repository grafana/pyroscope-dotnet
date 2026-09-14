using System.Diagnostics;
using System.Runtime.CompilerServices;
using Pyroscope;

namespace Example;

/// <summary>
/// An endpoint whose work happens after an <c>await</c> -- the case a stack-sampling
/// profiler cannot attribute on its own. Once the awaited operation completes, the
/// continuation runs on a thread whose physical stack is rooted at the thread pool, and
/// <see cref="OrderAsync"/> is nowhere on it, so the CPU burned below shows up as its own
/// tree instead of as work the endpoint caused.
///
/// <see cref="AsyncScope"/> is what closes that gap: it rides the ExecutionContext, which
/// the runtime does carry across an await, so the profiler can put the endpoint back on
/// top of those samples.
/// </summary>
internal class AsyncOrderService
{
    /// <summary>
    /// Name of the scope the endpoint opens. The integration test asserts this shows up as
    /// the root of the stacks below, so keep the two in sync.
    /// </summary>
    internal const string ScopeName = "GET /async-order";

    /// <summary>
    /// Label the endpoint attaches to its work. Dynamic labels used to be per-thread, so they
    /// stopped at the first `await`; the integration test asserts that samples of the work below
    /// still carry this one.
    /// </summary>
    internal const string LabelKey = "endpoint";
    internal const string LabelValue = "async-order";

    private static readonly LabelSet Labels = LabelSet.Empty.BuildUpon().Add(LabelKey, LabelValue).Build();

    public Task<string> OrderAsync()
    {
        // The Task-returning overload: the labels stay attached for the whole operation, not just
        // up to its first await.
        return LabelsWrapper.Do(Labels, OrderCoreAsync);
    }

    private async Task<string> OrderCoreAsync()
    {
        using (AsyncScope.Push(ScopeName))
        {
            // A LongRunning task body runs on a thread created for it, so this thread
            // cannot be one that carried the scope for some earlier reason: if the
            // profiler attributes this to the endpoint, it can only be because the
            // ExecutionContext brought the scope along.
            await Task.Factory.StartNew(
                SpinOnDedicatedThread,
                CancellationToken.None,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);

            // Task.Delay never completes synchronously, so this resumes as a thread-pool
            // work item: the everyday shape of an awaited I/O call in a web app.
            await Task.Delay(20);

            SpinAfterAwait();
        }

        return "async order";
    }

    // NoInlining: these exist to be recognisable frames in the profile, and a one-line
    // forwarder is exactly what the JIT would inline away.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void SpinOnDedicatedThread() => Burn();

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void SpinAfterAwait() => Burn();

    /// <summary>
    /// Burns CPU for <see cref="BurnDuration"/> so the sampler has something to see. Named
    /// callers above stay on the stack, which is what the test matches on.
    /// </summary>
    private static void Burn()
    {
        var stopwatch = Stopwatch.StartNew();
        long accumulator = 0;
        while (stopwatch.Elapsed < BurnDuration)
        {
            // Chunked so the clock read does not dominate the samples.
            for (var i = 0; i < 2_000_000; i++)
            {
                accumulator += i % 7;
            }
        }

        GC.KeepAlive(accumulator);
    }

    private static readonly TimeSpan BurnDuration = TimeSpan.FromMilliseconds(120);
}
