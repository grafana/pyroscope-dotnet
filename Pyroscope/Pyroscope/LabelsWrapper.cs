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
}
