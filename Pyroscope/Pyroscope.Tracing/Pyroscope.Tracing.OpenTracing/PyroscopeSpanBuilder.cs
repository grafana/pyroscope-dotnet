using OpenTracing;
using OpenTracing.Tag;

namespace Pyroscope.Tracing.OpenTracing;

public class PyroscopeSpanBuilder : ISpanBuilder
{

    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";

    private readonly ISpanBuilder _delegate;

    public PyroscopeSpanBuilder(ISpanBuilder spanBuilder)
    {
        _delegate = spanBuilder;
    }

    public ISpan Start()
    {
        var span = _delegate.Start();
        ConnectSpanWithProfiling(span);
        return span;
    }

    public IScope StartActive()
    {
        var scope = _delegate.StartActive();
        ConnectSpanWithProfiling(scope.Span);
        return scope;
    }

    public IScope StartActive(bool finishSpanOnDispose)
    {
        var scope = _delegate.StartActive(finishSpanOnDispose);
        ConnectSpanWithProfiling(scope.Span);
        return scope;
    }

    private static void ConnectSpanWithProfiling(ISpan span)
    {
        try
        {
            var spanId = span.Context.SpanId;
            var spanIdLong = Convert.ToUInt64(spanId.ToUpper(), 16);

            Profiler.Instance.SetProfileId(spanIdLong);
            span.SetTag(ProfileIdSpanTagKey, spanId);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while setting profile id in profiler instance: {ex.Message}");
        }
    }

    public ISpanBuilder AsChildOf(ISpan parent)
    {
        _delegate.AsChildOf(parent);
        return this;
    }

    public ISpanBuilder AsChildOf(ISpanContext parent)
    {
        _delegate.AsChildOf(parent);
        return this;
    }

    public ISpanBuilder AddReference(string referenceType, ISpanContext referencedContext)
    {
        _delegate.AddReference(referenceType, referencedContext);
        return this;
    }

    public ISpanBuilder WithTag(string key, bool value)
    {
        _delegate.WithTag(key, value);
        return this;
    }

    public ISpanBuilder WithTag(string key, double value)
    {
        _delegate.WithTag(key, value);
        return this;
    }

    public ISpanBuilder WithTag(string key, int value)
    {
        _delegate.WithTag(key, value);
        return this;
    }

    public ISpanBuilder WithTag(string key, string value)
    {
        _delegate.WithTag(key, value);
        return this;
    }

    public ISpanBuilder WithTag(BooleanTag tag, bool value)
    {
        _delegate.WithTag(tag, value);
        return this;
    }

    public ISpanBuilder WithTag(IntOrStringTag tag, string value)
    {
        _delegate.WithTag(tag, value);
        return this;
    }

    public ISpanBuilder WithTag(IntTag tag, int value)
    {
        _delegate.WithTag(tag, value);
        return this;
    }

    public ISpanBuilder WithTag(StringTag tag, string value)
    {
        _delegate.WithTag(tag, value);
        return this;
    }

    public ISpanBuilder WithStartTimestamp(DateTimeOffset startTimestamp)
    {
        _delegate.WithStartTimestamp(startTimestamp);
        return this;
    }

    public ISpanBuilder IgnoreActiveSpan()
    {
        _delegate.IgnoreActiveSpan();
        return this;
    }
}
