// <copyright file="NullOrEmptyThreadNameCheck.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using Datadog.Profiler.IntegrationTests.Helpers;
using Xunit;
using Xunit.Abstractions;

namespace Datadog.Profiler.IntegrationTests.Bugs
{
    public class NullOrEmptyThreadNameCheck
    {
        private const string ScenarioNullOrEmptyThreadName = "--scenario 19";

        private readonly ITestOutputHelper _output;

        public NullOrEmptyThreadNameCheck(ITestOutputHelper output)
        {
            _output = output;
        }

        [TestAppFact("Samples.Computer01")]
        public void ShouldNotCrashWhenNullOrEmptyThreadName(string appName, string framework, string appAssembly)
        {
            var runner = new TestApplicationRunner(appName, framework, appAssembly, _output, commandLine: ScenarioNullOrEmptyThreadName);
            runner.Environment.SetVariable(EnvironmentVariables.WallTimeProfilerEnabled, "1");
            runner.Environment.SetVariable(EnvironmentVariables.CpuProfilerEnabled, "0");

            using var agent = MockDatadogAgent.CreateHttpAgent(runner.XUnitLogger);
            runner.Run(agent);

            // Pyroscope intentionally omits high-cardinality thread labels. Reaching
            // this assertion with profiles proves null/empty names did not crash it.
            Assert.True(agent.NbCallsOnProfilingEndpoint > 0);
            Assert.NotEqual(0, SamplesHelper.GetSamplesCount(runner.Environment.PprofDir));
        }
    }
}
