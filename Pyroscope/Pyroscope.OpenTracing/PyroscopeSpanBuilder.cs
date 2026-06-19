using System.Runtime.InteropServices;
using OpenTracing;
using OpenTracing.Tag;

namespace Pyroscope.OpenTracing;

public class PyroscopeSpanBuilder : ISpanBuilder
{
    private const string ProfileIdSpanTagKey = "pyroscope.profile.id";

    private readonly ISpanBuilder _delegate;
    private readonly ISpanContext? _parent;

    internal PyroscopeSpanBuilder(ISpanBuilder spanBuilder, ISpanContext? parent)
    {
        _delegate = spanBuilder;
        _parent = parent;
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
        return new PyroscopeScope(scope, _parent);
    }

    public IScope StartActive(bool finishSpanOnDispose)
    {
        var scope = _delegate.StartActive(finishSpanOnDispose);
        ConnectSpanWithProfiling(scope.Span);
        return new PyroscopeScope(scope, _parent);
    }

    private void ConnectSpanWithProfiling(ISpan span)
    {
        if (_parent != null)
        {
            return;
        }

        try
        {
            TryConvertSpanContext(
                span.Context,
                out var localRootSpanId,
                out var traceIdHi,
                out var traceIdLo);

            Profiler.Instance.SetSpanContext(localRootSpanId, traceIdHi, traceIdLo);
            span.SetTag(ProfileIdSpanTagKey, span.Context.SpanId);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Caught exception while setting profile id in profiler instance: {ex.Message}");
        }
    }

    internal static void TryConvertSpanContext(
        ISpanContext context,
        out ulong localRootSpanId,
        out ulong traceIdHi,
        out ulong traceIdLo)
    {
        localRootSpanId = 0;
        traceIdHi = 0;
        traceIdLo = 0;

        if (context.SpanId is not { Length: 16 } spanId ||
            context.TraceId is not { Length: 32 } traceId)
        {
            return;
        }

        Span<ulong> spanIdBuf = stackalloc ulong[1];
        Span<ulong> traceIdBuf = stackalloc ulong[2];
        var spanIdBytes = MemoryMarshal.Cast<ulong, byte>(spanIdBuf);
        var traceIdBytes = MemoryMarshal.Cast<ulong, byte>(traceIdBuf);

        try
        {
            Convert.FromHexString(spanId).CopyTo(spanIdBytes);
            Convert.FromHexString(traceId).CopyTo(traceIdBytes);
        }
        catch (FormatException)
        {
            return;
        }

        localRootSpanId = spanIdBuf[0];
        traceIdHi = traceIdBuf[0];
        traceIdLo = traceIdBuf[1];
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

    private sealed class PyroscopeScope : IScope
    {
        private readonly IScope _delegate;
        private readonly ISpanContext? _parent;

        public ISpan Span => _delegate.Span;

        internal PyroscopeScope(IScope _delegate, ISpanContext? _parent)
        {
            this._delegate = _delegate;
            this._parent = _parent;
        }

        public void Dispose()
        {
            _delegate.Dispose();
            if (_parent == null)
            {
                Profiler.Instance.SetSpanContext(0, 0, 0);
                return;
            }
        }
    }
}
