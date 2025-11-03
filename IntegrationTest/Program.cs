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

app.Run();
