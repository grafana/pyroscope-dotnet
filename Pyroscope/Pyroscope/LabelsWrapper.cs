namespace Pyroscope;

public static class LabelsWrapper
{
    public static void Do(LabelSet labels, Action a)
    {
        try
        {
            labels.Activate();
            a.Invoke();
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    public static void Do<T>(LabelSet labels, Action<T> a, T state)
    {
        try
        {
            labels.Activate();
            a.Invoke(state);
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    public static async Task DoAsync(LabelSet labels, Func<Task> a)
    {
        try
        {
            labels.Activate();
            await a();
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    public static async Task DoAsync<T>(LabelSet labels, T state, Func<T, Task> a)
    {
        try
        {
            labels.Activate();
            await a(state);
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    public static async Task<T> DoAsync<T>(LabelSet labels, Func<Task<T>> a)
    {
        try
        {
            labels.Activate();
            return await a();
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    public static async Task<TResult> DoAsync<TState, TResult>(LabelSet labels, TState state, Func<TState, Task<TResult>> a)
    {
        try
        {
            labels.Activate();
            return await a(state);
        }
        finally
        {
            ResetLabels(labels);
        }
    }

    private static void ResetLabels(LabelSet labels)
    {
        if (labels.Previous == null)
        {
            Profiler.Instance.ClearDynamicTags();
        }
        else
        {
            labels.Previous.Activate();
        }
    }
}
