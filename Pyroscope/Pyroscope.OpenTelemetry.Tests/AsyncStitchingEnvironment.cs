using NUnit.Framework;

namespace Pyroscope.OpenTelemetry.Tests;

// Async stack stitching is opt-in, and Profiler.Instance reads the switch once, when it is first
// touched. Setting it from a SetUpFixture rather than from a fixture's own setup is what puts it
// in place before whichever test gets there first initialises the singleton.
[SetUpFixture]
public class AsyncStitchingEnvironment
{
    [OneTimeSetUp]
    public void EnableAsyncStitching()
    {
        Environment.SetEnvironmentVariable("PYROSCOPE_ASYNC_STITCHING_ENABLED", "true");
    }
}
