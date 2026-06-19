// No-op change to trigger a release of the Pyroscope.OpenTelemetry package.

using System.Diagnostics;
using System.Runtime.InteropServices;
using OpenTelemetry;

namespace Pyroscope.OpenTelemetry;

public class PyroscopeSpanProcessor : BaseProcessor<Activity>
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";

    public override void OnStart(Activity data)
    {
        if (!IsRootSpan(data))
        {
            return;
        }

        try
        {
            ConvertSpanId(data, out var localRootSpanId, out var traceIdLo, out var traceIdHi);

            Profiler.Instance.SetSpanContext(localRootSpanId, traceIdHi, traceIdLo);
            data.AddTag(ProfileIdSpanTagKey, data.SpanId.ToString());
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while setting profile id in profiler instance: {ex.Message}");
        }
    }

    public override void OnEnd(Activity data)
    {
        if (IsRootSpan(data))
        {
            Profiler.Instance.SetSpanContext(0, 0, 0);
        }
    }

    private static bool IsRootSpan(Activity data)
    {
        var parent = data.Parent;
        return parent == null || parent.HasRemoteParent;
    }

    internal static void ConvertSpanId(Activity activity, out ulong spanId, out ulong traceIdLo, out ulong traceIdHi)
    {
        ConvertSpanId(activity.SpanId, activity.TraceId, out spanId, out traceIdLo, out traceIdHi);
    }

    internal static void ConvertSpanId(ActivitySpanId activitySpanId, ActivityTraceId activityTraceId, out ulong spanId, out ulong traceIdLo, out ulong traceIdHi)
    {
        Span<ulong> spanIdBuf = stackalloc ulong[1];
        Span<ulong> traceIdBuf = stackalloc ulong[2];
        activitySpanId.CopyTo(MemoryMarshal.Cast<ulong, byte>(spanIdBuf));
        activityTraceId.CopyTo(MemoryMarshal.Cast<ulong, byte>(traceIdBuf));
        spanId = spanIdBuf[0];
        traceIdHi = traceIdBuf[0];
        traceIdLo = traceIdBuf[1];
    }
}
