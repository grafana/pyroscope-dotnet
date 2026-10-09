// <copyright file="ContextTracker.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

namespace Pyroscope
{
    internal sealed class ContextTracker
    {

        private readonly ProfilerStatus _status;

        /// <summary>
        /// _traceContextPtr points to the per-thread block described by <see cref="SpanContext"/>.
        /// The profiler hands out one block per thread, so the pointer is cached per thread and
        /// the writes below only ever touch the calling thread's block.
        /// </summary>
        private readonly ThreadLocal<IntPtr> _traceContextPtr;


        public ContextTracker(ProfilerStatus status)
        {
            _status = status;
            _traceContextPtr = new ThreadLocal<IntPtr>();
        }

        public bool IsEnabled
        {
            get
            {
                return _status.IsProfilerReady;
            }
        }

        public void Set(ulong localRootSpanId, ulong traceIdHi, ulong traceIdLo)
        {
            Publish(new SpanContext(localRootSpanId, traceIdHi, traceIdLo));
        }


        public void Reset()
        {
            Publish(SpanContext.Zero);
        }

        // Writes ctx into the calling thread's block, so samples taken from this thread carry
        // that span until it is replaced.
        public void Publish(in SpanContext ctx)
        {
            if (!IsEnabled)
            {
                return;
            }

            if (!EnsureIsInitialized())
            {
                return;
            }

            var ctxPtr = _traceContextPtr.Value;

            if (ctxPtr == IntPtr.Zero)
            {
                return;
            }

            try
            {
                ctx.Write(ctxPtr);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ContextTracker] Exception in Publish: {ex.Message}");
            }
        }

        private bool EnsureIsInitialized()
        {
            try
            {
                // try to avoid thread abort deadly exceptions
                if (
                    ((Thread.CurrentThread.ThreadState & ThreadState.AbortRequested) == ThreadState.AbortRequested) ||
                    ((Thread.CurrentThread.ThreadState & ThreadState.Aborted) == ThreadState.Aborted))
                {
                    return false;
                }

                if (_traceContextPtr.IsValueCreated)
                {
                    return true;
                }
            }
            catch (Exception e)
            {
                // seen in a crash: weird but possible on shutdown probably if the object is disposed
                Console.WriteLine($"Disposed tracing context pointer wrapper for the thread {Environment.CurrentManagedThreadId.ToString()} {e.Message}");
                _traceContextPtr.Value = IntPtr.Zero;
                return false;
            }

            try
            {
                _traceContextPtr.Value = NativeInterop.GetTraceContextNativePointer();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ContextTracker] Exception in EnsureIsInitialized: {ex.Message}");
                _traceContextPtr.Value = IntPtr.Zero;
                return false;
            }

            return true;
        }
    }
}
