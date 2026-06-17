using System.Diagnostics;
using System.Runtime.InteropServices;
using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

[TestFixture]
public class PyroscopeSpanProcessorTests
{
    [Test]
    public void ConvertSpanId_ConvertsActivityIdsUsingProfilerByteOrder()
    {
        const string spanIdHex = "0102030405060708";
        const string traceIdHex = "112233445566778899aabbccddeeff00";
        var spanId = ActivitySpanId.CreateFromString(spanIdHex);
        var traceId = ActivityTraceId.CreateFromString(traceIdHex);

        PyroscopeSpanProcessor.ConvertSpanId(spanId, traceId, out var localRootSpanId, out var traceIdLo, out var traceIdHi);

        Assert.That(localRootSpanId, Is.EqualTo(0x0807060504030201UL));
        Assert.That(traceIdHi, Is.EqualTo(0x8877665544332211UL));
        Assert.That(traceIdLo, Is.EqualTo(0x00ffeeddccbbaa99UL));
        Assert.That(ToProfilerHex(localRootSpanId), Is.EqualTo(spanIdHex));
        Assert.That(ToProfilerHex(traceIdHi, traceIdLo), Is.EqualTo(traceIdHex));
    }

    private static string ToProfilerHex(params ulong[] values)
    {
        var bytes = MemoryMarshal.AsBytes(values.AsSpan());
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }
}
