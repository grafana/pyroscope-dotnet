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

app.MapGet("/healthz", () => "ok");

app.MapGet("/npe", () =>
{
    for (var i = 0; i < 1_000_000; i++)
    {
        NPE.Work();
    }
    return "NPE work";
});

app.MapGet("/alloc", () =>
{
    AllocWork.Work();
    return "alloc work";
});

// Example: POST /profiling?cpu=true&allocation=false&contention=true&exception=false
app.MapPost("/profiling", (bool? cpu, bool? allocation, bool? contention, bool? exception, ILoggerFactory loggerFactory) =>
{
    var log = loggerFactory.CreateLogger("Pyroscope.Profiling");
    var profiler = Pyroscope.Profiler.Instance;
    var changed = new Dictionary<string, bool>();

    if (cpu.HasValue)
    {
        log.LogInformation("Profiler.SetCPUTrackingEnabled({Value})", cpu.Value);
        profiler.SetCPUTrackingEnabled(cpu.Value);
        changed["cpu"] = cpu.Value;
    }
    if (allocation.HasValue)
    {
        log.LogInformation("Profiler.SetAllocationTrackingEnabled({Value})", allocation.Value);
        profiler.SetAllocationTrackingEnabled(allocation.Value);
        changed["allocation"] = allocation.Value;
    }
    if (contention.HasValue)
    {
        log.LogInformation("Profiler.SetContentionTrackingEnabled({Value})", contention.Value);
        profiler.SetContentionTrackingEnabled(contention.Value);
        changed["contention"] = contention.Value;
    }
    if (exception.HasValue)
    {
        log.LogInformation("Profiler.SetExceptionTrackingEnabled({Value})", exception.Value);
        profiler.SetExceptionTrackingEnabled(exception.Value);
        changed["exception"] = exception.Value;
    }

    return Results.Ok(changed);
});


app.Run();
