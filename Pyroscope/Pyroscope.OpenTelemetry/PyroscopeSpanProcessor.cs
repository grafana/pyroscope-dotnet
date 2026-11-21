// No-op change to trigger a release of the Pyroscope.OpenTelemetry package.

using System.Diagnostics;
using System.Runtime.InteropServices;
using OpenTelemetry;

namespace Pyroscope.OpenTelemetry;

public class PyroscopeSpanProcessor : BaseProcessor<Activity>
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";

    private readonly Action<ulong, ulong, ulong> writeContext;

    private readonly AsyncLocal<RootContext?> currentRoot;

    private sealed record RootContext(Activity Activity, ulong Span, ulong Hi, ulong Lo, RootContext? Previous)
    {
        // Captured execution contexts can outlive the span that created them.
        public volatile bool Ended;
    }

    public PyroscopeSpanProcessor() : this((span, hi, lo) => Profiler.Instance.SetSpanContext(span, hi, lo))
    {
    }

    internal PyroscopeSpanProcessor(Action<ulong, ulong, ulong> writeContext)
    {
        this.writeContext = writeContext;
        currentRoot = new AsyncLocal<RootContext?>(change => ApplyContext(change.CurrentValue));
    }

    public override void OnStart(Activity data)
    {
        if (!IsRootSpan(data))
        {
            return;
        }

        try
        {
            ConvertSpanId(data, out var localRootSpanId, out var traceIdLo, out var traceIdHi);

            currentRoot.Value = new RootContext(data, localRootSpanId, traceIdHi, traceIdLo, ActiveRoot(currentRoot.Value));
            data.AddTag(ProfileIdSpanTagKey, data.SpanId.ToString());
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while setting profile id in profiler instance: {ex.Message}");
        }
    }

    public override void OnEnd(Activity data)
    {
        if (!IsRootSpan(data)) return;

        var current = currentRoot.Value;
        for (var root = current; root != null; root = root.Previous)
        {
            if (root.Activity != data) continue;
            root.Ended = true;
            currentRoot.Value = ActiveRoot(current);
            break;
        }
    }

    private static RootContext? ActiveRoot(RootContext? root)
    {
        while (root is { Ended: true }) root = root.Previous;
        return root;
    }

    private void ApplyContext(RootContext? root)
    {
        root = ActiveRoot(root);
        try
        {
            writeContext(root?.Span ?? 0, root?.Hi ?? 0, root?.Lo ?? 0);
        }
        catch (Exception ex)
        {
            // Exceptions escaping an execution-context change callback can terminate the process.
            Console.WriteLine($"Caught exception while applying profiler span context: {ex.Message}");
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
