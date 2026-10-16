using System.Diagnostics;
using System.Runtime.CompilerServices;
using Pyroscope;

namespace Example;

// An endpoint whose work happens after an `await`: the continuation runs on a thread whose
// physical stack is rooted at the thread pool, with OrderAsync nowhere on it, so without a
// scope the CPU burned below shows up as its own tree instead of as work the endpoint caused.
internal class AsyncOrderService
{
    // The integration tests assert on these, so keep the two sides in sync.
    internal const string ScopeName = "GET /async-order";
    internal const string LabelKey = "endpoint";
    internal const string LabelValue = "async-order";

    private static readonly LabelSet Labels = LabelSet.Empty.BuildUpon().Add(LabelKey, LabelValue).Build();

    public Task<string> OrderAsync()
    {
        // The Task-returning overload, so the labels stay attached past the first await.
        return LabelsWrapper.Do(Labels, OrderCoreAsync);
    }

    private async Task<string> OrderCoreAsync()
    {
        using (AsyncScope.Push(ScopeName))
        {
            // A LongRunning task body runs on a thread created for it, so it cannot have
            // carried the scope for some earlier reason.
            await Task.Factory.StartNew(
                SpinOnDedicatedThread,
                CancellationToken.None,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);

            // Task.Delay never completes synchronously, so this resumes as a thread-pool work
            // item: the everyday shape of an awaited I/O call in a web app.
            await Task.Delay(20);

            SpinAfterAwait();
        }

        return "async order";
    }

    // NoInlining: these exist to be recognisable frames in the profile, which is exactly what
    // the JIT would inline away.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void SpinOnDedicatedThread() => Burn();

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void SpinAfterAwait() => Burn();

    // Burns CPU so the sampler has something to see, with the named callers above still on the
    // stack, which is what the tests match on.
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
