using Example;
using Pyroscope;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddSingleton<BikeService>();
builder.Services.AddSingleton<CarService>();
builder.Services.AddSingleton<OrderService>();
builder.Services.AddSingleton<ScooterService>();

var app = builder.Build();

if (IsEnabled("RIDESHARE_DISABLE_PROFILING_AT_STARTUP"))
{
    Profiler.Instance.SetCPUTrackingEnabled(false);
    Profiler.Instance.SetAllocationTrackingEnabled(false);
    Profiler.Instance.SetContentionTrackingEnabled(false);
    Profiler.Instance.SetExceptionTrackingEnabled(false);
}

app.MapGet("/bike", (BikeService service) =>
{
    service.Order(1);
    return "Bike ordered";
});

app.MapGet("/scooter", (ScooterService service) =>
{
    service.Order(2);
    return "Scooter ordered";
});

app.MapGet("/car", (CarService service) =>
{
    service.Order(3);
    return "Car ordered";
});

app.MapGet("/npe", () =>
{
    for (var i = 0; i < 1_000_000; i++)
    {
        NPE.Work();
    }
    return "NPE work";
});

app.MapPost("/profiling/cpu/{enabled:bool}", (bool enabled) =>
{
    Profiler.Instance.SetCPUTrackingEnabled(enabled);
    return Results.Ok(new { cpuTrackingEnabled = enabled });
});

app.MapPost("/profiling/all/{enabled:bool}", (bool enabled) =>
{
    Profiler.Instance.SetCPUTrackingEnabled(enabled);
    Profiler.Instance.SetAllocationTrackingEnabled(enabled);
    Profiler.Instance.SetContentionTrackingEnabled(enabled);
    Profiler.Instance.SetExceptionTrackingEnabled(enabled);
    return Results.Ok(new { profilingEnabled = enabled });
});

app.Run();

static bool IsEnabled(string variableName)
{
    return bool.TryParse(Environment.GetEnvironmentVariable(variableName), out var isEnabled) && isEnabled;
}
