using System.Runtime.InteropServices;
using OpenTracing;
using NUnit.Framework;

namespace Pyroscope.OpenTracing.Tests;

[TestFixture]
public class PyroscopeSpanBuilderTests
{
    [Test]
    public void TryConvertSpanContext_ConvertsIdsUsingProfilerByteOrder()
    {
        var context = new TestSpanContext(
            spanId: "0102030405060708",
            traceId: "112233445566778899aabbccddeeff00");

        PyroscopeSpanBuilder.TryConvertSpanContext(
            context,
            out var localRootSpanId,
            out var traceIdHi,
            out var traceIdLo);

        Assert.That(localRootSpanId, Is.EqualTo(0x0807060504030201UL));
        Assert.That(traceIdHi, Is.EqualTo(0x8877665544332211UL));
        Assert.That(traceIdLo, Is.EqualTo(0x00ffeeddccbbaa99UL));
        Assert.That(ToProfilerHex(localRootSpanId), Is.EqualTo(context.SpanId));
        Assert.That(ToProfilerHex(traceIdHi, traceIdLo), Is.EqualTo(context.TraceId));
    }

    [TestCase("01020304050607", "112233445566778899aabbccddeeff00")]
    [TestCase("0102030405060708", "112233445566778899aabbccddeeff")]
    [TestCase("zz02030405060708", "112233445566778899aabbccddeeff00")]
    [TestCase("0102030405060708", "zz2233445566778899aabbccddeeff00")]
    public void TryConvertSpanContext_SetsZeroContextForInvalidIds(string spanId, string traceId)
    {
        PyroscopeSpanBuilder.TryConvertSpanContext(
            new TestSpanContext(spanId, traceId),
            out var localRootSpanId,
            out var traceIdHi,
            out var traceIdLo);

        Assert.That(localRootSpanId, Is.Zero);
        Assert.That(traceIdHi, Is.Zero);
        Assert.That(traceIdLo, Is.Zero);
    }

    private static string ToProfilerHex(params ulong[] values)
    {
        var bytes = MemoryMarshal.AsBytes(values.AsSpan());
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }

    private sealed class TestSpanContext : ISpanContext
    {
        public TestSpanContext(string spanId, string traceId)
        {
            SpanId = spanId;
            TraceId = traceId;
        }

        public string TraceId { get; }

        public string SpanId { get; }

        public IEnumerable<KeyValuePair<string, string>> GetBaggageItems()
        {
            return Enumerable.Empty<KeyValuePair<string, string>>();
        }
    }
}
