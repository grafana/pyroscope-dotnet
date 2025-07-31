# Span profiles for OpenTelemetry

This package enables applications that already rely on [OpenTelemetry](https://opentelemetry.io/docs/instrumentation/net/getting-started/) for distributed tracing and Pyroscope for continuous profiling to link the tracing and profiling data together.

See [Link tracing and profiling with Span Profiles](https://grafana.com/docs/pyroscope/latest/configure-client/trace-span-profiles/) for more information.

## Prerequisites

- Your .NET application is instrumented with [Pyroscope's profiler](https://grafana.com/docs/pyroscope/latest/configure-client/language-sdks/dotnet/)
- Your .NET application is instrumented (manually) with [OpenTelemetry](https://opentelemetry.io/docs/instrumentation/net/getting-started/)

## Integration

Add the following package to your project:

```shell
dotnet add package Pyroscope.OpenTelemetry
```

Register the `PyroscopeSpanProcessor` in your OpenTelemetry integration:

```csharp
builder.Services.AddOpenTelemetry()
    .WithTracing(b =>
    {
        b
        .AddAspNetCoreInstrumentation()
        .AddConsoleExporter()
        .AddOtlpExporter()
        .AddProcessor(new Pyroscope.OpenTelemetry.PyroscopeSpanProcessor());
    });
```
