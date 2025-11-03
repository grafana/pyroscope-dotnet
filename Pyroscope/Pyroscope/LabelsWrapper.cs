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
