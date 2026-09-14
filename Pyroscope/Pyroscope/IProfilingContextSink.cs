namespace Pyroscope
{
    /// <summary>
    /// The profiler operations that ambient context propagation needs. Abstracted so that
    /// <see cref="ProfilingContext"/> can be tested without the native library.
    /// </summary>
    internal interface IProfilingContextSink
    {
        /// <summary>
        /// Interns an async scope named <paramref name="name"/> under
        /// <paramref name="parentScopeId"/> and returns the resulting chain's id, or 0 when the
        /// profiler cannot record it.
        /// </summary>
        uint InternAsyncScope(uint parentScopeId, string name);

        /// <summary>
        /// Interns a label set and returns its id, 0 while the profiler is still starting up, or
        /// <see cref="ProfilingContextSink.NeverInterned"/> when the set can never be interned (propagation is off,
        /// the store is full, or the process has run out of label keys).
        /// </summary>
        uint InternDynamicTagSet(string[] keys, string[] values);

        /// <summary>
        /// Publishes the async scope chain and label set of the code running on the calling
        /// thread; samples taken from this thread are attributed to them until replaced. 0 means
        /// "none"; <see cref="ProfilingContextSink.NeverInterned"/> as the tag set means "leave the thread's tags
        /// alone, the caller applies them itself".
        /// </summary>
        void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId);

        /// <summary>
        /// Sets one dynamic tag on the calling thread. Used for label sets that could not be
        /// interned, which are applied the pre-propagation way: on the thread that set them.
        /// </summary>
        void SetDynamicTag(string key, string value);

        /// <summary>Clears the calling thread's dynamic tags.</summary>
        void ClearDynamicTags();

        /// <summary>Publishes the span identity of the code running on the calling thread.</summary>
        void SetSpanContext(in SpanContext context);
    }

    internal static class ProfilingContextSink
    {
        /// <summary>
        /// Mirrors NoDynamicTagSetEver in PInvoke.h: a label set that will never be interned, so
        /// there is no point asking again.
        /// </summary>
        public const uint NeverInterned = uint.MaxValue;
    }
}
