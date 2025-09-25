using System.Globalization;
using Pyroscope;

namespace Example;

internal class HelicopterService
{
    private readonly OrderService _orderService;

    public HelicopterService(OrderService orderService)
    {
        _orderService = orderService;
    }

    public async Task Order(int searchRadius)
    {
        _orderService.FindNearestVehicle(searchRadius, "helicopter");
        await DoWorkAsync(searchRadius);
    }

    private static async Task DoWorkAsync(int searchRadius)
    {
        var labels = LabelSet.Empty.BuildUpon()
            .Add("search_radius", searchRadius.ToString(CultureInfo.InvariantCulture))
            .Build();

        await LabelsWrapper.DoAsync(labels, async () =>
        {
            for (long i = 0; i < 1_000; i++)
            {
                await Task.Yield();
            }
        });
    }
}
