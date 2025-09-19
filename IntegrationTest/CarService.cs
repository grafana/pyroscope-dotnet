namespace Example;

internal class CarService(OrderService orderService)
{
    public void Order(int searchRadius) =>
        orderService.FindNearestVehicle(searchRadius, "car");
}
