using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Pyroscope
{
    /// <summary>
    /// The span identity the profiler stamps on samples, and the layout it expects to find in
    /// the per-thread block it handed us:
    /// <code>
    /// offset size(bytes)
    ///                    |--------------------|
    ///   0        8       |     WriteGuard     |    // 8-byte for the alignment
    ///                    |--------------------|
    ///   8        8       | Local Root Span Id |
    ///                    |--------------------|
    ///   16       8       |      TraceIdHi     |
    ///                    |--------------------|
    ///   24       8       |      TraceIdLo     |
    ///                    |--------------------|
    /// </code>
    /// The guard lets the profiler's sampling thread tell that a write is in progress and
    /// avoid a torn read (using memory barriers).
    /// </summary>
    internal readonly struct SpanContext : IEquatable<SpanContext>
    {
        public static readonly SpanContext Zero = new(0, 0, 0);

        public readonly ulong LocalRootSpanId;
        public readonly ulong TraceIdHi;
        public readonly ulong TraceIdLo;

        public SpanContext(ulong localRootSpanId, ulong traceIdHi, ulong traceIdLo)
        {
            LocalRootSpanId = localRootSpanId;
            TraceIdHi = traceIdHi;
            TraceIdLo = traceIdLo;
        }

        public bool IsZero => LocalRootSpanId == 0 && TraceIdHi == 0 && TraceIdLo == 0;

        [MethodImpl(MethodImplOptions.NoInlining)]
        public void Write(IntPtr ptr)
        {
            // Set the WriteGuard
            Marshal.WriteInt64(ptr, 1);
            Thread.MemoryBarrier();

            // Using WriteInt64 to write 2 long values is ~8x faster than using Marshal.StructureToPtr
            // For the offset, we follow the layout depicted above
            Marshal.WriteInt64(ptr + 8, (long)LocalRootSpanId);
            Marshal.WriteInt64(ptr + 16, (long)TraceIdHi);
            Marshal.WriteInt64(ptr + 24, (long)TraceIdLo);

            // Reset the WriteGuard
            Thread.MemoryBarrier();
            Marshal.WriteInt64(ptr, 0);
        }

        public bool Equals(SpanContext other) =>
            LocalRootSpanId == other.LocalRootSpanId && TraceIdHi == other.TraceIdHi && TraceIdLo == other.TraceIdLo;

        public override bool Equals(object? obj) => obj is SpanContext other && Equals(other);

        public override int GetHashCode() => HashCode.Combine(LocalRootSpanId, TraceIdHi, TraceIdLo);
    }
}
