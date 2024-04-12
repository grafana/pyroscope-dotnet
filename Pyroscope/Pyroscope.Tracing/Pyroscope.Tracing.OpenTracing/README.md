## Span profiles for OpenTracing

This package enables application that already rely on OpenTracing for tracing and Pyroscope for profiling to link the tracing and profiling data. 

See https://grafana.com/docs/pyroscope/latest/configure-client/trace-span-profiles/ for more information.

### Integration

Add the package to your project:

```shell
dotnet add package Pyroscope.Tracing.OpenTracing
```

Wrap your existing tracer with the `PyroscopeTracer`:

```csharp

// obtain the OpenTracing tracer (note, this can be done in different ways)
var tracingConfig = Configuration.FromEnv(loggerFactory);
var tracer = tracingConfig.GetTracer();

// wrap the OpenTracing tracer with the PyroscopeTracer and register it
builder.Services.AddSingleton(new PyroscopeTracer(tracer));
```

### Example

TODO: Link to an example project
