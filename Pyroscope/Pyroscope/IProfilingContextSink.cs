namespace Pyroscope
{
    // The profiler operations that ambient context propagation needs. Abstracted so that
    // ProfilingContext can be tested without the native library.
    internal interface IProfilingContextSink
    {
        // Returns the interned chain's id, or 0 when the profiler cannot record it.
        uint InternAsyncScope(uint parentScopeId, string name);

        // Returns the set's id, 0 while the profiler is still starting up, or NeverInterned
        // when the set can never be interned (propagation is off, the store is full, or the
        // process has run out of label keys).
        uint InternDynamicTagSet(string[] keys, string[] values);

        // Samples taken from the calling thread are attributed to this scope chain and label
        // set until replaced. 0 means "none"; NeverInterned as the tag set means "leave the
        // thread's tags alone, the caller applies them itself".
        void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId);

        // Used for label sets that could not be interned, which are applied the
        // pre-propagation way: on the thread that set them.
        void SetDynamicTag(string key, string value);

        void ClearDynamicTags();

        void SetSpanContext(in SpanContext context);
    }

    internal static class ProfilingContextSink
    {
        // Mirrors NoDynamicTagSetEver in PInvoke.h: a label set that will never be interned,
        // so there is no point asking again.
        public const uint NeverInterned = uint.MaxValue;
    }
}
