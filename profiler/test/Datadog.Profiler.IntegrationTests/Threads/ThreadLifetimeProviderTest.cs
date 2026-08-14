// <copyright file="ThreadLifetimeProviderTest.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using Datadog.Profiler.IntegrationTests.Helpers;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.Threads
{
    public class ThreadLifetimeProviderTest
    {
        private readonly ITestOutputHelper _output;

        public ThreadLifetimeProviderTest(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void ShouldGetThreadLifetimeSamples(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: "--scenario 14");
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.ThreadLifetimeEnabled, "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "thread_lifetime_timeline").Should().NotBeEmpty();
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void ShouldNotGetThreadLifetimeSamplesWhenDisabled(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: "--scenario 14");
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.WallTimeProfilerEnabled, "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "thread_lifetime_timeline").Should().BeEmpty();
        }
    }
}
