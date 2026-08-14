// <copyright file="AllocationsProfilerTest.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using System.Linq;
using Datadog.Profiler.IntegrationTests.Helpers;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.Allocations
{
    public class AllocationsProfilerTest
    {
        private readonly ITestOutputHelper _output;

        public AllocationsProfilerTest(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void ShouldGetAllocationSamples(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: "--scenario 9");
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.AllocationProfilerEnabled, "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            agent.NbCallsOnProfilingEndpoint.Should().BeGreaterThan(0);
            SamplesHelper.CheckSamplesValueCount(runner.Environment.PprofDir, 2);
            SamplesHelper.GetSamples(runner.Environment.PprofDir, "alloc_samples").Should().NotBeEmpty();
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void ExplicitlyDisableAllocationProfiler(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: "--scenario 9");
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.WallTimeProfilerEnabled, "1");
            runner.Environment.SetVariable(EnvironmentVariables.AllocationProfilerEnabled, "0");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "alloc_samples").Should().BeEmpty();
            SamplesHelper.GetProfiles(runner.Environment.PprofDir).Should().NotBeEmpty();
        }
    }
}
