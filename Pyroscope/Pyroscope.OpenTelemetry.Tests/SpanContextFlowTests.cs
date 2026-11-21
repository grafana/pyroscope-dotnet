using System.Diagnostics;
using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

[TestFixture]
public class SpanContextFlowTests
{
    [Test]
    public void RootContextFollowsExecutionOntoAnotherThread()
    {
        using var slot = new ThreadLocal<(ulong Span, ulong Hi, ulong Lo)>();
        using var processor = new PyroscopeSpanProcessor((span, hi, lo) => slot.Value = (span, hi, lo));
        using var root = new Activity("request").SetIdFormat(ActivityIdFormat.W3C).Start();
        PyroscopeSpanProcessor.ConvertSpanId(root, out var span, out var lo, out var hi);
        var origin = Environment.CurrentManagedThreadId;
        ExecutionContext? continuation = null;
        ExecutionContext.Run(ExecutionContext.Capture()!, _ =>
        {
            processor.OnStart(root);
            Assert.That(slot.Value, Is.EqualTo((span, hi, lo)));
            continuation = ExecutionContext.Capture();
        }, null);

        var originAfterYield = slot.Value;
        var workerId = 0;
        (ulong, ulong, ulong) during = default, after = default, replay = default;
        var worker = new Thread(() =>
        {
            workerId = Environment.CurrentManagedThreadId;
            ExecutionContext.Run(continuation!, _ =>
            {
                during = slot.Value;
                processor.OnEnd(root);
            }, null);
            after = slot.Value;
            ExecutionContext.Run(continuation!, _ => replay = slot.Value, null);
        });
        worker.UnsafeStart();
        worker.Join();

        Assert.That(workerId, Is.Not.EqualTo(origin));
        Assert.That(during, Is.EqualTo((span, hi, lo)));
        Assert.That(originAfterYield, Is.EqualTo((0UL, 0UL, 0UL)));
        Assert.That(after, Is.EqualTo((0UL, 0UL, 0UL)));
        Assert.That(replay, Is.EqualTo((0UL, 0UL, 0UL)));
    }
}
