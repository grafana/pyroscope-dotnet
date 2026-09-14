namespace Pyroscope;

/// <summary>
/// An immutable set of labels the profiler attaches to samples.
///
/// Build one with <see cref="BuildUpon"/> and make it current for a piece of work with
/// <see cref="LabelsWrapper"/> -- which, unlike setting labels directly, keeps them attached
/// across <c>await</c>.
///
/// Prefer values with bounded cardinality: every distinct combination becomes its own series
/// in Pyroscope, and the profiler retains it for the process lifetime.
/// </summary>
public class LabelSet
{
    private readonly Dictionary<string, string> _labels;

    /// <summary>
    /// Id the profiler interned this set under, or null until it has been resolved.
    /// A single long so that readers cannot see a half-written value; -1 means unresolved,
    /// which is also how a set that the profiler was not yet ready to record stays retryable.
    /// </summary>
    private long _nativeTagSetId = -1;

    private string? _contentKey;
    private string[]? _keys;
    private string[]? _values;

    public static readonly LabelSet Empty = new(new Dictionary<string, string>());

    private LabelSet(Dictionary<string, string> labels)
    {
        _labels = labels;
    }

    /// <summary>
    /// Applies this set to the calling thread and, when the profiler supports it, to the rest of
    /// the current async flow -- so samples taken from an <c>await</c> continuation carry it too.
    ///
    /// Leaves the set in place; prefer <see cref="LabelsWrapper.Push"/> or
    /// <see cref="LabelsWrapper.Do(LabelSet, Action)"/>, which restore the previous set on the
    /// way out.
    /// </summary>
    public void Activate()
    {
        Profiler.Instance.ProfilingContext.PushLabels(this);
    }

    public Builder BuildUpon()
    {
        return new Builder(this);
    }

    internal Dictionary<string, string> Labels => _labels;

    internal int Count => _labels.Count;

    internal uint? NativeTagSetId
    {
        get
        {
            var value = Volatile.Read(ref _nativeTagSetId);
            return value < 0 ? null : (uint)value;
        }

        set => Volatile.Write(ref _nativeTagSetId, value.HasValue ? value.Value : -1);
    }

    /// <summary>
    /// Keys and values as parallel arrays, in a stable order, for handing to the profiler.
    /// </summary>
    internal string[] Keys
    {
        get
        {
            EnsureArrays();
            return _keys!;
        }
    }

    internal string[] Values
    {
        get
        {
            EnsureArrays();
            return _values!;
        }
    }

    /// <summary>
    /// Identifies this set by content, so two sets built separately for the same request shape
    /// share one interned id instead of filling the profiler's store.
    /// </summary>
    internal string ContentKey
    {
        get
        {
            var key = _contentKey;
            if (key != null)
            {
                return key;
            }

            EnsureArrays();

            // Length-prefixed, so no combination of separators inside a key or value can make
            // two different sets look alike.
            var builder = new System.Text.StringBuilder();
            for (var i = 0; i < _keys!.Length; i++)
            {
                builder.Append(_keys[i].Length).Append(':').Append(_keys[i])
                       .Append(_values![i].Length).Append(':').Append(_values[i]);
            }

            key = builder.ToString();
            _contentKey = key;
            return key;
        }
    }

    private void EnsureArrays()
    {
        if (_keys != null)
        {
            return;
        }

        var keys = new string[_labels.Count];
        var values = new string[_labels.Count];
        var i = 0;
        // Sorted, so the content key does not depend on insertion order.
        foreach (var key in _labels.Keys.OrderBy(static k => k, StringComparer.Ordinal))
        {
            keys[i] = key;
            values[i] = _labels[key];
            i++;
        }

        _values = values;
        _keys = keys;
    }

    public class Builder
    {
        private readonly Dictionary<string, string> _labels;

        public Builder(LabelSet prev)
        {
            _labels = new Dictionary<string, string>(prev._labels);
        }

        public Builder Add(string k, string v)
        {
            _labels[k] = v;
            return this;
        }

        public LabelSet Build()
        {
            // Copy, so a builder reused after Build() cannot mutate the set that was already
            // handed out -- which now matters, because LabelSet caches its interned id and
            // content key on the assumption that it never changes.
            return new LabelSet(new Dictionary<string, string>(_labels));
        }
    }
}
