using OpenTracing;
using OpenTracing.Propagation;

namespace Pyroscope.Tracing.OpenTracing;

public class PyroscopeTracer : ITracer
{
    private readonly ITracer _delegate;
    public IScopeManager ScopeManager { get; }
    public ISpan? ActiveSpan => ScopeManager.Active?.Span;
    private readonly Config _config;

    private PyroscopeTracer(ITracer tracerDelegate)
    {
        _config = new Config();
        _delegate = tracerDelegate;
        ScopeManager = tracerDelegate.ScopeManager;
    }

    public ISpanBuilder BuildSpan(string operationName)
    {
        return new PyroscopeSpanBuilder(_config, _delegate.BuildSpan(operationName), ActiveSpan?.Context);
    }

    public void Inject<TCarrier>(ISpanContext spanContext, IFormat<TCarrier> format, TCarrier carrier)
    {
        _delegate.Inject(spanContext, format, carrier);
    }

    public ISpanContext Extract<TCarrier>(IFormat<TCarrier> format, TCarrier carrier)
    {
        return _delegate.Extract(format, carrier);
    }

    public class Builder
    {
        private readonly PyroscopeTracer _tracer;

        public Builder(ITracer tracerDelegate)
        {
            _tracer = new PyroscopeTracer(tracerDelegate);
        }

        public Builder WithRootSpanOnly(bool RootSpanOnly)
        {
            _tracer._config.RootSpanOnly = RootSpanOnly;
            return this;
        }

        public PyroscopeTracer Build()
        {
            return _tracer;
        }
    }
}
