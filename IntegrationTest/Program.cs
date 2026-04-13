using Example;
using Pyroscope;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddSingleton<BikeService>();
builder.Services.AddSingleton<CarService>();
builder.Services.AddSingleton<OrderService>();
builder.Services.AddSingleton<ScooterService>();

var app = builder.Build();

if (Environment.GetEnvironmentVariable("DYNAMIC_CPU_DISABLED_AT_START") == "true")
{
    Profiler.Instance.SetCPUTrackingEnabled(false);
    Console.WriteLine("Dynamic CPU toggle: disabled CPU profiling at startup");
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

app.MapPost("/profiling/cpu/enable", () =>
{
    Profiler.Instance.SetCPUTrackingEnabled(true);
    return "CPU profiling enabled";
});

app.MapPost("/profiling/cpu/disable", () =>
{
    Profiler.Instance.SetCPUTrackingEnabled(false);
    return "CPU profiling disabled";
});

app.Run();
