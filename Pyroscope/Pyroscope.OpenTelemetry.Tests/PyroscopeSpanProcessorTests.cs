using System.Diagnostics;
using System.Runtime.InteropServices;
using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

[TestFixture]
public class PyroscopeSpanProcessorTests
{
    [TestCase(true)]
    [TestCase(false)]
    public void OnStart_TagsOnlyTheLocalRoot(bool remoteParent)
    {
        using var source = new ActivitySource(nameof(OnStart_TagsOnlyTheLocalRoot));
        using var listener = new ActivityListener
        {
            ShouldListenTo = candidate => candidate == source,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllData,
        };
        ActivitySource.AddActivityListener(listener);
        var parent = remoteParent
            ? new ActivityContext(ActivityTraceId.CreateRandom(), ActivitySpanId.CreateRandom(), ActivityTraceFlags.Recorded, isRemote: true)
            : default;
        using var processor = new PyroscopeSpanProcessor();
        using var root = source.StartActivity("request", ActivityKind.Server, parent)!;
        processor.OnStart(root);
        using var child = source.StartActivity("child")!;
        processor.OnStart(child);

        Assert.That(root.GetTagItem("pyroscope.profile.id"), Is.EqualTo(root.SpanId.ToString()));
        Assert.That(child.GetTagItem("pyroscope.profile.id"), Is.Null);
    }

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
