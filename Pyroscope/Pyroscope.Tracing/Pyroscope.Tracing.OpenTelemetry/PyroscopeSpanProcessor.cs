using System.Diagnostics;
using OpenTelemetry;

namespace Pyroscope.Tracing.OpenTelemetry;

public class PyroscopeSpanProcessor : BaseProcessor<Activity>
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";
    private readonly Config _config;

    private PyroscopeSpanProcessor()
    {
        _config = new Config();
    }

    public override void OnStart(Activity data)
    {
        if (_config.RootSpanOnly && !IsRootSpan(data))
        {
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

    public override void OnEnd(Activity data)
    {
        if (IsRootSpan(data))
        {
            Profiler.Instance.SetProfileId(0); // TODO: Replace with ResetContext()
            return;
        }
        if (_config.RootSpanOnly)
        {
            return;
        }

        try
        {
            var parentSpanId = data.ParentSpanId.ToString();
            var spanIdLong = Convert.ToUInt64(parentSpanId.ToUpper(), 16);
            Profiler.Instance.SetProfileId(spanIdLong);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while restoring previous profile id in profiler instance: {ex.Message}");
        }
    }

    private static bool IsRootSpan(Activity data)
    {
        var parent = data.Parent;
        return parent == null || parent.HasRemoteParent;
    }

    internal class Config
    {
        public bool RootSpanOnly { get; set; } = true;
    }

    public class Builder
    {
        private PyroscopeSpanProcessor _processor = new PyroscopeSpanProcessor();

        public Builder WithRootSpanOnly(bool RootSpanOnly)
        {
            _processor._config.RootSpanOnly = RootSpanOnly;
            return this;
        }

        public PyroscopeSpanProcessor Build()
        {
            return _processor;
        }
    }
}
