namespace Example;

internal class ScooterService(OrderService orderService)
{
    public void Order(int searchRadius)
    {
        for (long i = 0; i < 2_000_000_000; i++)
        {
        }

        OrderInternal(searchRadius);
        DoSomeOtherWork();
    }

    private void OrderInternal(int searchRadius)
    {
        orderService.FindNearestVehicle(searchRadius, "scooter");
    }

    private static void DoSomeOtherWork()
    {
        for (long i = 0; i < 1_000_000_000; i++)
        {
        }
    }
}
