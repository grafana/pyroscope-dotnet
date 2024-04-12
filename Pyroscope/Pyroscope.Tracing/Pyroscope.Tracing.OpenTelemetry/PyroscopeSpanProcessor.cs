using System.Diagnostics;
using OpenTelemetry;

namespace Pyroscope.Tracing.OpenTelemetry;

public class PyroscopeSpanProcessor : BaseProcessor<Activity>
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";

    public override void OnStart(Activity data)
    {
        if (!IsRootSpan(data)) {
            return;
        }

        try
        {
            var spanId = data.SpanId.ToString();
            var spanIdLong = Convert.ToUInt64(spanId.ToUpper(), 16);

            // Establish a two-way connection between the span and profiling data
            Profiler.Instance.SetProfileId(spanIdLong);
            data.AddTag(ProfileIdSpanTagKey, spanId);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while setting profile id in profiler instance: {ex.Message}");
        }

    }

    private static bool IsRootSpan(Activity data)
    {
        var parent = data.Parent;
        return parent == null || parent.HasRemoteParent;
    }
}
