using System.Collections.Concurrent;

namespace Pyroscope.Tests;

/// <summary>
/// Stands in for the native profiler, modelling the parts the propagation tests depend on:
/// interned async scopes and label sets, and the per-OS-thread slots the sampler reads.
///
/// The fidelity matters. <c>SetCurrentProfilingContext</c> replaces a thread's tags wholesale
/// (that is what a <c>Tags</c> assignment does natively), and <see cref="ProfilingContextSink.NeverInterned"/>
/// means "leave them alone" -- so a test asserting on <see cref="LabelsOnThread"/> is asserting
/// on what the profiler would actually stamp onto a sample.
/// </summary>
internal sealed class FakeProfiler : IProfilingContextSink
{
    private readonly ConcurrentDictionary<string, uint> _tagSetIdsByContent = new();
    private readonly ConcurrentDictionary<uint, Dictionary<string, string>> _tagSetsById = new();
    private readonly ConcurrentDictionary<int, ThreadSlot> _threads = new();
    private readonly ConcurrentQueue<string> _internedTagSets = new();
    private int _nextTagSetId;
    private int _nextScopeId;

    /// <summary>When set, InternDynamicTagSet returns this instead of interning: 0 emulates a
    /// profiler that is still starting up, NeverInterned a full store or disabled propagation.</summary>
    public uint? InternTagSetResult { get; set; }

    /// <summary>How many distinct label sets were actually interned.</summary>
    public int InternedTagSetCount => _internedTagSets.Count;

    private int _publishCount;

    /// <summary>How many times a thread's slots were actually written.</summary>
    public int PublishCount => Volatile.Read(ref _publishCount);

    public uint InternAsyncScope(uint parentScopeId, string name)
    {
        return (uint)Interlocked.Increment(ref _nextScopeId);
    }

    public uint InternDynamicTagSet(string[] keys, string[] values)
    {
        if (InternTagSetResult.HasValue)
        {
            return InternTagSetResult.Value;
        }

        var content = string.Join(",", keys.Zip(values, (k, v) => $"{k}={v}"));
        return _tagSetIdsByContent.GetOrAdd(content, _ =>
        {
            var id = (uint)Interlocked.Increment(ref _nextTagSetId);
            var set = new Dictionary<string, string>();
            for (var i = 0; i < keys.Length; i++)
            {
                set[keys[i]] = values[i];
            }
            _tagSetsById[id] = set;
            _internedTagSets.Enqueue(content);
            return id;
        });
    }

    public void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId)
    {
        Interlocked.Increment(ref _publishCount);
        var slot = Slot(Environment.CurrentManagedThreadId);
        slot.ScopeId = asyncScopeId;

        if (dynamicTagSetId == ProfilingContextSink.NeverInterned)
        {
            // "The caller applies these itself": the thread's tags are not ours to touch.
            return;
        }

        // Natively this is a whole-Tags assignment, so anything already on the thread goes.
        slot.TagSetId = dynamicTagSetId;
        slot.DirectTags.Clear();
    }

    public void SetDynamicTag(string key, string value)
    {
        var slot = Slot(Environment.CurrentManagedThreadId);
        slot.DirectTags[key] = value;
    }

    public void ClearDynamicTags()
    {
        var slot = Slot(Environment.CurrentManagedThreadId);
        slot.TagSetId = 0;
        slot.DirectTags.Clear();
    }

    public void SetSpanContext(in SpanContext context)
    {
        Slot(Environment.CurrentManagedThreadId).Span = context;
    }

    /// <summary>The async scope chain id a sample taken on that thread would carry.</summary>
    public uint ScopeOnThread(int managedThreadId) =>
        _threads.TryGetValue(managedThreadId, out var slot) ? slot.ScopeId : uint.MaxValue;

    /// <summary>The span a sample taken on that thread would carry.</summary>
    public SpanContext SpanOnThread(int managedThreadId) =>
        _threads.TryGetValue(managedThreadId, out var slot) ? slot.Span : SpanContext.Zero;

    /// <summary>
    /// The labels a sample taken on that thread would carry, whether they got there via an
    /// interned set or key by key. Null when the thread was never touched at all.
    /// </summary>
    public IReadOnlyDictionary<string, string>? LabelsOnThread(int managedThreadId)
    {
        if (!_threads.TryGetValue(managedThreadId, out var slot))
        {
            return null;
        }

        if (slot.TagSetId != 0 && _tagSetsById.TryGetValue(slot.TagSetId, out var interned))
        {
            return interned;
        }

        return new Dictionary<string, string>(slot.DirectTags);
    }

    public IReadOnlyDictionary<string, string> LabelsHere() =>
        LabelsOnThread(Environment.CurrentManagedThreadId) ?? new Dictionary<string, string>();

    private ThreadSlot Slot(int managedThreadId) => _threads.GetOrAdd(managedThreadId, _ => new ThreadSlot());

    private sealed class ThreadSlot
    {
        public uint ScopeId;
        public uint TagSetId;
        public SpanContext Span;
        public readonly Dictionary<string, string> DirectTags = new();
    }
}
