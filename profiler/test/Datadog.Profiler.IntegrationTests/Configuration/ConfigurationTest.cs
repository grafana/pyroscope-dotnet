// <copyright file="ConfigurationTest.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using System.IO;
using Datadog.Profiler.IntegrationTests.Helpers;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.Configuration
{
    public class ConfigurationTest
    {
        private readonly ITestOutputHelper _output;

        public ConfigurationTest(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void PyroscopeConfigurationProducesProfiles(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: "--scenario 4");
            runner.Environment.SetVariable("PYROSCOPE_APPLICATION_NAME", "upstream-configuration-test");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            Assert.True(agent.NbCallsOnProfilingEndpoint > 0);
            Assert.NotEmpty(Directory.GetFiles(runner.Environment.PprofDir, "*.pprof"));
        }

        [TestAppFact("Samples.Computer01", new[] { "net10.0" })]
        public void DisabledProfilerProducesNoProfiles(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(
                appName,
                framework,
                appAssembly,
                _output,
                commandLine: "--scenario 4",
                enableProfiler: false);

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            Assert.Equal(0, agent.NbCallsOnProfilingEndpoint);
            Assert.Empty(Directory.GetFiles(runner.Environment.PprofDir, "*.pprof"));
        }
    }
}
