namespace Pyroscope;

public class LabelSet
{
    private readonly Dictionary<string, string> _labels;
    internal readonly LabelSet? Previous;

    public static readonly LabelSet Empty = new(new Dictionary<string, string>());

    private LabelSet(Dictionary<string, string> labels)
    {
        _labels = labels;
        Previous = null;
    }


    private LabelSet(LabelSet previous, Dictionary<string, string> labels)
    {
        _labels = labels;
        Previous = previous;
    }

    public void Activate()
    {
        Profiler.Instance.ClearDynamicTags();
        foreach (var (key, value) in _labels)
        {
            Profiler.Instance.SetDynamicTag(key, value);
        }
    }

    public Builder BuildUpon()
    {
        return new Builder(this);
    }

    public class Builder
    {
        private readonly LabelSet _prev;
        private readonly Dictionary<string, string> _labels;

        public Builder(LabelSet prev)
        {
            _prev = prev;
            _labels = new Dictionary<string, string>(_prev._labels);
        }

        public Builder Add(string k, string v)
        {
            _labels[k] = v;
            return this;
        }

        public LabelSet Build()
        {
            return new LabelSet(_prev, _labels);
        }
    }
}
