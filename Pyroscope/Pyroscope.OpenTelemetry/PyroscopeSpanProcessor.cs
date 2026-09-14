// No-op change to trigger a release of the Pyroscope.OpenTelemetry package.

using System.Diagnostics;
using System.Runtime.InteropServices;
using OpenTelemetry;

namespace Pyroscope.OpenTelemetry;

public class PyroscopeSpanProcessor : BaseProcessor<Activity>
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";
    private const string AsyncScopeProperty = "pyroscope.async.scope";

    private readonly bool _stitchAsyncStacks;

    public PyroscopeSpanProcessor()
        : this(stitchAsyncStacks: true)
    {
    }

    /// <param name="stitchAsyncStacks">
    /// When true (the default), each span also opens a <see cref="AsyncScope"/> named after
    /// the span. Because the scope rides the ExecutionContext, samples taken from the
    /// span's <c>await</c> continuations -- which run on thread-pool threads whose physical
    /// stack no longer mentions the caller -- are attributed back to the span, so the
    /// flamegraph nests a request's work under the request instead of scattering it under
    /// thread-pool dispatch roots. Pass false, or set
    /// <c>PYROSCOPE_ASYNC_STITCHING_ENABLED=false</c>, to report physical stacks only.
    /// </param>
    public PyroscopeSpanProcessor(bool stitchAsyncStacks)
    {
        _stitchAsyncStacks = stitchAsyncStacks;
    }

    public override void OnStart(Activity data)
    {
        if (_stitchAsyncStacks)
        {
            try
            {
                // Every span, not just the root: nested spans give the flamegraph the same
                // shape the trace has, with the sampled physical stacks hanging off it.
                data.SetCustomProperty(AsyncScopeProperty, AsyncScope.Push(GetScopeName(data)));
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Caught exception while opening a Pyroscope async scope: {ex.Message}");
            }
        }

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

        if (data.GetCustomProperty(AsyncScopeProperty) is AsyncScope scope)
        {
            try
            {
                scope.Dispose();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Caught exception while closing a Pyroscope async scope: {ex.Message}");
            }
        }
    }

    // DisplayName is what an OpenTelemetry exporter shows for the span: for ASP.NET Core
    // that is the route template ("GET /folders/{id}"), which is the low-cardinality name
    // we want as a frame. It falls back to OperationName for sources that leave it unset.
    private static string GetScopeName(Activity data)
    {
        return string.IsNullOrEmpty(data.DisplayName) ? data.OperationName : data.DisplayName;
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
