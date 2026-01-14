// <copyright file="ContextTracker.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Pyroscope
{
    internal sealed class ContextTracker
    {

        private readonly ProfilerStatus _status;

        /// <summary>
        /// _traceContextPtr points to a structure with this layout
        /// The structure is as follow:
        /// offset size(bytes)
        ///                    |--------------------|
        ///   0        8       |     WriteGuard     |    // 8-byte for the alignment
        ///                    |--------------------|
        ///   8        8       | Local Root Span Id |
        ///                    |--------------------|
        ///   16       8       |       Span Id      |
        ///                    |--------------------|
        /// This allows us to inform the profiler sampling thread when we are writing or not the data
        /// and avoid torn read/write (Using memory barriers).
        /// We take advantage of this layout in SpanContext.Write
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

        public void Set(ulong localRootSpanId, ulong spanId)
        {
            WriteToNative(new SpanContext(localRootSpanId, spanId));
        }


        public void Reset()
        {
            WriteToNative(SpanContext.Zero);
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

        private void WriteToNative(in SpanContext ctx)
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
                Console.WriteLine($"[ContextTracker] Exception in WriteToNative: {ex.Message}");
            }
        }

        // See the description and the layout depicted above
        private readonly struct SpanContext
        {
            public static readonly SpanContext Zero = new(0, 0);

            public readonly ulong LocalRootSpanId;
            public readonly ulong SpanId;

            public SpanContext(ulong localRootSpanId, ulong spanId)
            {
                LocalRootSpanId = localRootSpanId;
                SpanId = spanId;
            }

            [MethodImpl(MethodImplOptions.NoInlining)]
            public void Write(IntPtr ptr)
            {
                // Set the WriteGuard
                Marshal.WriteInt64(ptr, 1);
                Thread.MemoryBarrier();

                // Using WriteInt64 to write 2 long values is ~8x faster than using Marshal.StructureToPtr
                // For the offset, we follow the layout depicted above
                Marshal.WriteInt64(ptr + 8, (long)LocalRootSpanId);
                // Marshal.WriteInt64(ptr + 16, (long)SpanId);

                // Reset the WriteGuard
                Thread.MemoryBarrier();
                Marshal.WriteInt64(ptr, 0);
            }
        }
    }
}
