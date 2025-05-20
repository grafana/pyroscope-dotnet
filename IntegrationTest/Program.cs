using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Collections;

using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

namespace Example;

public static class Program
{
    private static readonly List<FileStream> Files = new();
    public static void Main(string[] args)
    {
        var orderService = new OrderService();
        var bikeService = new BikeService(orderService);
        var scooterService = new ScooterService(orderService);
        var carService = new CarService(orderService);

        var builder = WebApplication.CreateBuilder(args);
        var app = builder.Build();

        app.MapGet("/bike", () =>
        {
            bikeService.Order(1);
            return "Bike ordered";
        });
        app.MapGet("/scooter", () =>
        {
            scooterService.Order(2);
            return "Scooter ordered";
        });

        app.MapGet("/car", () =>
        {
            carService.Order(3);
            return "Car ordered";
        });

        app.Run();
    }
}
