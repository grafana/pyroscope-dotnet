namespace Pyroscope
{
    /// <summary>
    /// Routes ambient profiling context to the native profiler.
    /// </summary>
    internal sealed class NativeProfilingContextSink : IProfilingContextSink
    {
        private readonly ContextTracker _contextTracker;

        public NativeProfilingContextSink(ContextTracker contextTracker)
        {
            _contextTracker = contextTracker;
        }

        public uint InternAsyncScope(uint parentScopeId, string name)
        {
            return NativeInterop.PushAsyncScope(parentScopeId, name);
        }

        public uint InternDynamicTagSet(string[] keys, string[] values)
        {
            return NativeInterop.InternDynamicTagSet(keys, values, keys.Length);
        }

        public void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId)
        {
            NativeInterop.SetCurrentProfilingContext(asyncScopeId, dynamicTagSetId);
        }

        public void SetDynamicTag(string key, string value)
        {
            NativeInterop.SetDynamicTag(key, value);
        }

        public void ClearDynamicTags()
        {
            NativeInterop.ClearDynamicTags();
        }

        public void SetSpanContext(in SpanContext context)
        {
            // Not a P/Invoke: the profiler hands each thread a block to write into, and
            // ContextTracker caches that pointer per thread.
            _contextTracker.Publish(context);
        }
    }
}
