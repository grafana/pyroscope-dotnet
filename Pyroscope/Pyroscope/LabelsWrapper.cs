namespace Pyroscope;

/// Runs work with a LabelSet attached to the profiler's samples.
///
/// With PYROSCOPE_ASYNC_PROFILING_ENABLED set, the labels follow the work across `await` and a
/// thread stops advertising them once the flow leaves it. While it is off they cover the thread
/// that set them, up to its first `await`.
///
/// Use the Task-returning overloads, or Push, for async work: the Action overloads restore the
/// labels as soon as the delegate returns, which for an async lambda would be at its first
/// await.
public static class LabelsWrapper
{
    public static void Do(LabelSet labels, Action a)
    {
        using (Push(labels))
        {
            a.Invoke();
        }
    }

    public static void Do<T>(LabelSet labels, Action<T> a, T state)
    {
        using (Push(labels))
        {
            a.Invoke(state);
        }
    }

    public static async Task Do(LabelSet labels, Func<Task> a)
    {
        using (Push(labels))
        {
            await a.Invoke().ConfigureAwait(false);
        }
    }

    public static async Task<TResult> Do<TResult>(LabelSet labels, Func<Task<TResult>> a)
    {
        using (Push(labels))
        {
            return await a.Invoke().ConfigureAwait(false);
        }
    }

    public static async Task Do<T>(LabelSet labels, Func<T, Task> a, T state)
    {
        using (Push(labels))
        {
            await a.Invoke(state).ConfigureAwait(false);
        }
    }

    public static async Task<TResult> Do<T, TResult>(LabelSet labels, Func<T, Task<TResult>> a, T state)
    {
        using (Push(labels))
        {
            return await a.Invoke(state).ConfigureAwait(false);
        }
    }

    /// Makes `labels` current until the returned scope is disposed. Use this when the work does
    /// not fit a callback; see the README for an example.
    public static LabelScope Push(LabelSet labels)
    {
        var context = Profiler.Instance.ProfilingContext;
        var previous = context.PushLabels(labels);
        return new LabelScope(context, labels, previous);
    }
}
