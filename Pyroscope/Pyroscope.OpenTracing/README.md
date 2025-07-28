# Span profiles for OpenTracing

This package enables applications that already rely on [OpenTracing](https://opentracing.io/guides/csharp/) for distributed tracing and Pyroscope for continuous profiling to link the tracing and profiling data together.

See [Link tracing and profiling with Span Profiles](https://grafana.com/docs/pyroscope/latest/configure-client/trace-span-profiles/) for more information.

## Prerequisites

- Your .NET application is instrumented with [Pyroscope's profiler](https://grafana.com/docs/pyroscope/latest/configure-client/language-sdks/dotnet/)
- Your .NET application is instrumented with [OpenTracing](https://opentracing.io/guides/csharp/)

## Integration

Add the following package to your project:

```shell
dotnet add package Pyroscope.OpenTracing
```

Wrap your existing tracer with the `PyroscopeTracer` and register it globally:

```csharp
// obtain the OpenTracing tracer (note that this can vary between applications)
var tracingConfig = Configuration.FromEnv(loggerFactory);
var tracer = tracingConfig.GetTracer();

// wrap the OpenTracing tracer with the PyroscopeTracer and register it
GlobalTracer.Register(new Pyroscope.OpenTracing.PyroscopeTracer(tracer));
```
