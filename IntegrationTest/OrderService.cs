using System.Runtime.InteropServices;
using System.Threading;
using Pyroscope;

namespace Example;

internal class OrderService
{
    [DllImport("libc")]
    static extern int syscall(int number);

    
    public void FindNearestVehicle(long searchRadius, string vehicle)
    {

        int tid = syscall(186);
        Console.WriteLine($"Thread id {tid}");
        lock (_lock)
        {
            var labels = LabelSet.Empty.BuildUpon()
                .Add("vehicle", vehicle)
                .Build();

            LabelsWrapper.Do(labels, static (state) =>
            {
                

                if (string.Equals("car", state.vehicle))
                {
                    for (long i = 0; i < 100_0000; i++)
                    {
                        NPE.work();
                    }
                    CheckDriverAvailability(state.labels, state.searchRadius);
                }
                else
                {
                    for (long i = 0; i < 100_000_000; i++)
                    {
                        
                    }
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
            for (long i = 0; i < 100_000_000; i++)
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
        for (long i = 0; i < 100_000_000; i++)
        {
                        
        }
    }
}
