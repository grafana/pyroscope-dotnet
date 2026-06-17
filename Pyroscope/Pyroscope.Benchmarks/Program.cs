using System.Diagnostics;
using System.Buffers.Binary;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using BenchmarkDotNet.Attributes;
using BenchmarkDotNet.Running;
using Pyroscope.OpenTelemetry;

BenchmarkSwitcher.FromAssembly(typeof(Program).Assembly).Run(args);

[MemoryDiagnoser]
public class SpanIdParsingBenchmark
{
    private static readonly Activity Activity;
    private static readonly ulong ExpectedSpanId;
    private static readonly ulong ExpectedTraceIdLo;
    private static readonly ulong ExpectedTraceIdHi;

    static SpanIdParsingBenchmark()
    {
        Activity = new Activity(nameof(SpanIdParsingBenchmark));
        Activity.SetIdFormat(ActivityIdFormat.W3C);
        Activity.Start();
        if (Activity.SpanId.ToHexString().Length != 16)
        {
            throw new Exception("invalid SpanId");
        }
        if (Activity.TraceId.ToHexString().Length != 32)
        {
            throw new Exception("invalid TraceId");
        }

        PyroscopeSpanProcessor.ConvertSpanId(Activity, out ExpectedSpanId, out ExpectedTraceIdLo, out ExpectedTraceIdHi);
        ConvertWithSeparateByteBuffers(Activity, out var separateSpanId, out var separateTraceIdLo, out var separateTraceIdHi);
        AssertMatchesCurrent("SeparateByteBuffers", separateSpanId, separateTraceIdLo, separateTraceIdHi);

        ConvertWithBinaryPrimitivesLittleEndian(Activity, out var binarySpanId, out var binaryTraceIdLo, out var binaryTraceIdHi);
        AssertMatchesCurrent("BinaryPrimitivesLittleEndian", binarySpanId, binaryTraceIdLo, binaryTraceIdHi);

        ConvertWithUnsafeReadUnaligned(Activity, out var unsafeSpanId, out var unsafeTraceIdLo, out var unsafeTraceIdHi);
        AssertMatchesCurrent("UnsafeReadUnaligned", unsafeSpanId, unsafeTraceIdLo, unsafeTraceIdHi);

        var spanIdHex = Activity.SpanId.ToHexString();
        var traceIdHex = Activity.TraceId.ToHexString();
        var displaySpanId = Convert.ToUInt64(spanIdHex, 16);
        var displayTraceIdHi = Convert.ToUInt64(traceIdHex[..16], 16);
        var displayTraceIdLo = Convert.ToUInt64(traceIdHex[16..], 16);

        Console.WriteLine($"SpanId: {spanIdHex}");
        Console.WriteLine($"TraceId: {traceIdHex}");
        Console.WriteLine($"Current numeric IDs match display-order hex: span={ExpectedSpanId == displaySpanId}, traceHi={ExpectedTraceIdHi == displayTraceIdHi}, traceLo={ExpectedTraceIdLo == displayTraceIdLo}");
    }


    [Benchmark(Baseline = true)]
    public ulong CurrentMemoryMarshalCast()
    {
        PyroscopeSpanProcessor.ConvertSpanId(Activity, out var spanId, out var traceIdLo, out var traceIdHi);
        return Combine(spanId, traceIdLo, traceIdHi);
    }

    [Benchmark]
    public ulong SeparateByteBuffers()
    {
        ConvertWithSeparateByteBuffers(Activity, out var spanId, out var traceIdLo, out var traceIdHi);
        return Combine(spanId, traceIdLo, traceIdHi);
    }

    [Benchmark]
    public ulong BinaryPrimitivesLittleEndian()
    {
        ConvertWithBinaryPrimitivesLittleEndian(Activity, out var spanId, out var traceIdLo, out var traceIdHi);
        return Combine(spanId, traceIdLo, traceIdHi);
    }

    [Benchmark]
    public ulong UnsafeReadUnaligned()
    {
        ConvertWithUnsafeReadUnaligned(Activity, out var spanId, out var traceIdLo, out var traceIdHi);
        return Combine(spanId, traceIdLo, traceIdHi);
    }

    private static void ConvertWithSeparateByteBuffers(Activity activity, out ulong spanId, out ulong traceIdLo, out ulong traceIdHi)
    {
        Span<byte> spanIdBuf = stackalloc byte[8];
        Span<byte> traceIdBuf = stackalloc byte[16];
        activity.SpanId.CopyTo(spanIdBuf);
        activity.TraceId.CopyTo(traceIdBuf);
        spanId = MemoryMarshal.Read<ulong>(spanIdBuf);
        traceIdHi = MemoryMarshal.Read<ulong>(traceIdBuf);
        traceIdLo = MemoryMarshal.Read<ulong>(traceIdBuf[8..]);
    }

    private static void ConvertWithBinaryPrimitivesLittleEndian(Activity activity, out ulong spanId, out ulong traceIdLo, out ulong traceIdHi)
    {
        Span<byte> spanIdBuf = stackalloc byte[8];
        Span<byte> traceIdBuf = stackalloc byte[16];
        activity.SpanId.CopyTo(spanIdBuf);
        activity.TraceId.CopyTo(traceIdBuf);
        spanId = BinaryPrimitives.ReadUInt64LittleEndian(spanIdBuf);
        traceIdHi = BinaryPrimitives.ReadUInt64LittleEndian(traceIdBuf);
        traceIdLo = BinaryPrimitives.ReadUInt64LittleEndian(traceIdBuf[8..]);
    }

    private static void ConvertWithUnsafeReadUnaligned(Activity activity, out ulong spanId, out ulong traceIdLo, out ulong traceIdHi)
    {
        Span<byte> spanIdBuf = stackalloc byte[8];
        Span<byte> traceIdBuf = stackalloc byte[16];
        activity.SpanId.CopyTo(spanIdBuf);
        activity.TraceId.CopyTo(traceIdBuf);

        ref var spanIdStart = ref MemoryMarshal.GetReference(spanIdBuf);
        ref var traceIdStart = ref MemoryMarshal.GetReference(traceIdBuf);
        spanId = Unsafe.ReadUnaligned<ulong>(ref spanIdStart);
        traceIdHi = Unsafe.ReadUnaligned<ulong>(ref traceIdStart);
        traceIdLo = Unsafe.ReadUnaligned<ulong>(ref Unsafe.Add(ref traceIdStart, 8));
    }

    private static void AssertMatchesCurrent(string name, ulong spanId, ulong traceIdLo, ulong traceIdHi)
    {
        if (spanId != ExpectedSpanId || traceIdLo != ExpectedTraceIdLo || traceIdHi != ExpectedTraceIdHi)
        {
            throw new InvalidOperationException($"{name} changed ID semantics");
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static ulong Combine(ulong spanId, ulong traceIdLo, ulong traceIdHi)
    {
        return spanId ^ traceIdLo ^ traceIdHi;
    }
}
