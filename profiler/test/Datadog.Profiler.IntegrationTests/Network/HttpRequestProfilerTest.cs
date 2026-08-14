// <copyright file="HttpRequestProfilerTest.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using Datadog.Profiler.IntegrationTests.Helpers;
using FluentAssertions;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.Network
{
    public class HttpRequestProfilerTest
    {
        private readonly ITestOutputHelper _output;

        public HttpRequestProfilerTest(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.ParallelCountSites", new[] { "net10.0" })]
        public void ShouldNotGetHttpSamplesWhenDefaultSampling(string appName, string framework, string appAssembly)
        {
            var runner = CreateRunner(appName, framework, appAssembly, "--iterations 5 --scenario 7");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "request_time").Should().BeEmpty();
        }

        [TestAppFact("Samples.ParallelCountSites", new[] { "net10.0" })]
        public void ShouldGetHttpSamples(string appName, string framework, string appAssembly)
        {
            var runner = CreateRunner(appName, framework, appAssembly, "--iterations 5 --scenario 7");
            runner.Environment.SetVariable(EnvironmentVariables.ForceHttpSamplingEnabled, "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "request_time").Should().NotBeEmpty();
            SamplesHelper.IsLabelPresent(runner.Environment.PprofDir, "request url").Should().BeTrue();
        }

        [TestAppFact("Samples.ParallelCountSites", new[] { "net10.0" })]
        public void ShouldGetError(string appName, string framework, string appAssembly)
        {
            var runner = CreateRunner(appName, framework, appAssembly, "--iterations 5 --scenario 2");
            runner.Environment.SetVariable(EnvironmentVariables.ForceHttpSamplingEnabled, "1");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            SamplesHelper.GetSamples(runner.Environment.PprofDir, "request_time").Should().NotBeEmpty();
            SamplesHelper.IsLabelPresent(runner.Environment.PprofDir, "response error").Should().BeTrue();
        }

        private TestApplicationRunner CreateRunner(string appName, string framework, string appAssembly, string commandLine)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine);
            EnvironmentHelper.DisableDefaultProfilers(runner);
            runner.Environment.SetVariable(EnvironmentVariables.HttpProfilingEnabled, "1");
            return runner;
        }
    }
}
