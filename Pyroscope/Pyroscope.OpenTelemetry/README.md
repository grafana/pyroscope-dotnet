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

## Async stack stitching

With `PYROSCOPE_ASYNC_PROFILING_ENABLED=true`, `PyroscopeSpanProcessor` also opens a [Pyroscope
async scope](https://github.com/grafana/pyroscope-dotnet) per span, named after the span. The
switch is off by default, and the processor reports physical stacks only until it is set.

This matters because the profiler samples physical call stacks: after an `await` the
continuation runs on a thread-pool thread whose stack no longer mentions the method that
awaited, so an async-heavy service's flamegraph scatters each request's work under
thread-pool dispatch roots. A span scope rides the `ExecutionContext`, which the runtime
*does* carry across an await, so the profiler can put the span back above those samples: the
flamegraph nests a request's work under the request, and inclusive time per endpoint covers
the whole request rather than only the part before its first `await`.

The same switch makes the span identity itself follow the async flow, so a span's `await`
continuations are attributed to it rather than only its synchronous prologue.

To keep span profiles but leave the stacks alone even where async profiling is enabled
process-wide, construct the processor with `new PyroscopeSpanProcessor(stitchAsyncStacks: false)`.
