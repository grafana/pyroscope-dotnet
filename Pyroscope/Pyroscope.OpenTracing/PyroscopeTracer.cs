using OpenTracing;
using OpenTracing.Propagation;

namespace Pyroscope.OpenTracing;

public class PyroscopeTracer : ITracer
{
    private readonly ITracer _delegate;
    public IScopeManager ScopeManager { get; }
    public ISpan? ActiveSpan => ScopeManager.Active?.Span;

    public PyroscopeTracer(ITracer tracerDelegate)
    {
        _delegate = tracerDelegate;
        ScopeManager = tracerDelegate.ScopeManager;
    }

    public ISpanBuilder BuildSpan(string operationName)
    {
        return new PyroscopeSpanBuilder(_delegate.BuildSpan(operationName), ActiveSpan?.Context);
    }

    public void Inject<TCarrier>(ISpanContext spanContext, IFormat<TCarrier> format, TCarrier carrier)
    {
        _delegate.Inject(spanContext, format, carrier);
    }

    public ISpanContext Extract<TCarrier>(IFormat<TCarrier> format, TCarrier carrier)
    {
        return _delegate.Extract(format, carrier);
    }
}
