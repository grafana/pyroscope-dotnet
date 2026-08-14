// <copyright file="GarbageCollectorCpuTimeTest.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using Datadog.Profiler.IntegrationTests.Helpers;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.GarbageCollections
{
    public class GarbageCollectorCpuTimeTest
    {
        private readonly ITestOutputHelper _output;

        public GarbageCollectorCpuTimeTest(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void CheckCpuTimeForGcThreadsIsReported(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(
                appName,
                framework,
                appAssembly,
                _output,
                commandLine: "--scenario 26 --param 20000 --threads 4");
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.CpuProfilerEnabled, "1");
            runner.Environment.SetVariable(EnvironmentVariables.GcThreadsCpuTimeEnabled, "1");
            runner.Environment.SetVariable("DOTNET_gcServer", "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            agent.NbCallsOnProfilingEndpoint.Should().BeGreaterThan(0);
            SamplesHelper.IsLabelPresent(runner.Environment.PprofDir, "gc_cpu_sample").Should().BeTrue();
        }

    }
}
