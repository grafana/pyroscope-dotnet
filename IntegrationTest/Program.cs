using Example;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddSingleton<BikeService>();
builder.Services.AddSingleton<CarService>();
builder.Services.AddSingleton<OrderService>();
builder.Services.AddSingleton<ScooterService>();

var app = builder.Build();

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

app.MapPost("/profiling/repro-dynamic-cpu-toggle", () =>
{
    Pyroscope.Profiler.Instance.SetCPUTrackingEnabled(false);
    Pyroscope.Profiler.Instance.SetAllocationTrackingEnabled(false);
    Pyroscope.Profiler.Instance.SetContentionTrackingEnabled(false);
    Pyroscope.Profiler.Instance.SetExceptionTrackingEnabled(false);

    Parallel.For(0, Environment.ProcessorCount * 4, _ =>
    {
        var value = 0d;
        for (var i = 1; i < 50_000; i++)
        {
            value += Math.Sqrt(i);
        }

        GC.KeepAlive(value);
    });

    Pyroscope.Profiler.Instance.SetCPUTrackingEnabled(true);
    return Results.Ok("CPU tracking re-enabled");
});


app.Run();
