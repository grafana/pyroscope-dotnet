using OpenTelemetry.Trace;
using Pyroscope;

var builder = WebApplication.CreateBuilder(args);

// Add services to the container
builder.Services.AddControllers();

// Configure OpenTelemetry with Pyroscope
builder.Services.AddOpenTelemetry()
    .WithTracing(tracerProviderBuilder =>
    {
        tracerProviderBuilder
            .AddAspNetCoreInstrumentation()
            .AddHttpClientInstrumentation()
            .AddProcessor(new Pyroscope.OpenTelemetry.PyroscopeSpanProcessor());
    });

var app = builder.Build();

// Configure Pyroscope profiler
var enable = true;
Pyroscope.Profiler.Instance.SetCPUTrackingEnabled(enable);
Pyroscope.Profiler.Instance.SetAllocationTrackingEnabled(enable);
Pyroscope.Profiler.Instance.SetContentionTrackingEnabled(enable);
Pyroscope.Profiler.Instance.SetExceptionTrackingEnabled(enable);

// Configure the HTTP request pipeline

app.UseHttpsRedirection();
app.UseAuthorization();
app.MapControllers();

app.Run();