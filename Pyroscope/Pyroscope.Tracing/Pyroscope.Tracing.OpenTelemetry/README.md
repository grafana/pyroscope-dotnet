## Span profiles for OpenTelemetry

This package enables application that already rely on OpenTelemetry for tracing and Pyroscope for profiling to link the tracing and profiling data. 

See https://grafana.com/docs/pyroscope/latest/configure-client/trace-span-profiles/ for more information.

### Integration

Add the package to your project:

```shell
dotnet add package Pyroscope.Tracing.OpenTelemetry
```

Register the `PyroscopeSpanProcessor`:

```csharp

builder.Services.AddOpenTelemetry()
    .WithTracing(b =>
    {
        b
        .AddAspNetCoreInstrumentation()
        .AddConsoleExporter()
        .AddOtlpExporter()
        .AddProcessor(new PyroscopeSpanProcessor());
    });

```

### Example

TODO: Link to an example project
