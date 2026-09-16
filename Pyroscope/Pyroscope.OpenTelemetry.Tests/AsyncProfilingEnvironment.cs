using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

// Async profiling is opt-in, and Profiler.Instance reads the switch once, when it is first
// touched. Setting it from a SetUpFixture rather than from a fixture's own setup is what puts it
// in place before whichever test gets there first initialises the singleton.
[SetUpFixture]
public class AsyncProfilingEnvironment
{
    [OneTimeSetUp]
    public void EnableAsyncProfiling()
    {
        Environment.SetEnvironmentVariable("PYROSCOPE_ASYNC_PROFILING_ENABLED", "true");
    }
}
