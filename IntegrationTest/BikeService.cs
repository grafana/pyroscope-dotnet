namespace Example;

internal class BikeService(OrderService orderService)
{
    public void Order(int searchRadius) =>
        orderService.FindNearestVehicle(searchRadius, "bike");
}
