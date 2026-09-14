namespace Pyroscope;

/// <summary>
/// Runs work with a <see cref="LabelSet"/> attached to the profiler's samples.
///
/// The labels follow the work across <c>await</c>: they ride the <c>ExecutionContext</c>, so a
/// continuation that resumes on a thread-pool thread carries them too, and the thread stops
/// advertising them once the flow leaves it.
///
/// Use the <see cref="Task"/>-returning overloads (or <see cref="Push"/>) for async work. The
/// <see cref="Action"/> overloads run to completion before the labels are restored, which for an
/// <c>async</c> lambda would mean "up to its first await" -- so an async lambda binds to the
/// <c>Func&lt;Task&gt;</c> overloads instead.
/// </summary>
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

    /// <summary>
    /// Makes <paramref name="labels"/> current until the returned scope is disposed. Use this when
    /// the work does not fit a callback:
    ///
    /// <code>
    /// using (Pyroscope.LabelsWrapper.Push(labels))
    /// {
    ///     await DoWorkAsync();
    /// }
    /// </code>
    /// </summary>
    public static LabelScope Push(LabelSet labels)
    {
        var context = Profiler.Instance.ProfilingContext;
        var previous = context.PushLabels(labels);
        return new LabelScope(context, labels, previous);
    }
}
