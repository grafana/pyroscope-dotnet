using Pyroscope;

namespace Example;

internal class OrderService
{
    public void FindNearestVehicle(long searchRadius, string vehicle)
    {
        lock (_lock)
        {
            var labels = LabelSet.Empty.BuildUpon()
                .Add("vehicle", vehicle)
                .Build();

            LabelsWrapper.Do(labels, static (state) =>
            {
                for (long i = 0; i < state.searchRadius * 1_000_000_000; i++)
                {
                }

                if (string.Equals("car", state.vehicle))
                {
                    CheckDriverAvailability(state.labels, state.searchRadius);
                }
            },
            (labels, searchRadius, vehicle));
        }
    }

    private readonly object _lock = new();

    private static void CheckDriverAvailability(LabelSet labels, long searchRadius)
    {
        var region = Environment.GetEnvironmentVariable("REGION") ?? "unknown_region";
        labels = labels.BuildUpon()
            .Add("driver_region", region)
            .Build();

        LabelsWrapper.Do(labels, static (state) =>
        {
            for (long i = 0; i < state.searchRadius * 1_000_000_000; i++)
            {
            }

            var forceMutexLock = DateTime.Now.Minute % 2 == 0;

            if (string.Equals("eu-north", state.region) && forceMutexLock)
            {
                MutexLock(state.searchRadius);
            }
        },
        (region, searchRadius));
    }

    private static void MutexLock(long searchRadius)
    {
        for (long i = 0; i < 30 * searchRadius * 1_000_000_000; i++)
        {
        }
    }
}
