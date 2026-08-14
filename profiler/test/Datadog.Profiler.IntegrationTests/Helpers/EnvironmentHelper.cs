// <copyright file="EnvironmentHelper.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Datadog.Profiler.IntegrationTests.Xunit;
using Datadog.Profiler.SmokeTests;

namespace Datadog.Profiler.IntegrationTests.Helpers
{
    public class EnvironmentHelper
    {
        private static string _solutionDirectory = null;
        private readonly string _framework;
        private readonly string _testOutputPath;
        private readonly bool _enableProfiler;

        public EnvironmentHelper(string framework, bool enableTracer, bool enableProfiler)
        {
            _framework = framework;
            _testOutputPath = BuildTestOutputPath(framework);
            _enableProfiler = enableProfiler;

            if (enableTracer)
            {
                throw new NotSupportedException("The Pyroscope fork does not include the Datadog tracer.");
            }

            InitializeLogAndPprofEnvironmentVariables();
        }

        public static bool IsAlpine
        {
            get
            {
                var s = Environment.GetEnvironmentVariable("IsAlpine");
                return "true".Equals(s, StringComparison.OrdinalIgnoreCase);
            }
        }

        public Dictionary<string, string> CustomEnvironmentVariables { get; set; } = new Dictionary<string, string>();

        public string LogDir
        {
            get
            {
                return CustomEnvironmentVariables[EnvironmentVariables.ProfilingLogDir];
            }
        }

        public string PprofDir
        {
            get
            {
                return CustomEnvironmentVariables[EnvironmentVariables.ProfilingPprofDir];
            }
        }

        public static string GetBinOutputPath()
        {
            return Path.Combine(GetRootOutputDir(), "bin");
        }

        public static bool IsRunningOnWindows()
        {
            return RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        }

        public static string GetPlatform()
        {
            return RuntimeInformation.ProcessArchitecture switch
            {
                Architecture.Arm64 => "ARM64",
                Architecture.X86 => "x86",
                _ => "x64",
            };
        }

        public static bool IsRunningInCi() =>
            !string.IsNullOrEmpty(System.Environment.GetEnvironmentVariable("CI")) ||
            !string.IsNullOrEmpty(System.Environment.GetEnvironmentVariable("DD_TESTING_OUPUT_DIR"));

        internal static string GetConfiguration()
        {
#if DEBUG
            return "Debug";
#else
            return "Release";
#endif
        }

        internal static void DisableDefaultProfilers(TestApplicationRunner runner)
        {
            DisableDefaultProfilers(runner.Environment);
        }

        internal static void DisableDefaultProfilers(SmokeTestRunner runner)
        {
            DisableDefaultProfilers(runner.EnvironmentHelper);
        }

        internal void EnableTracer()
        {
            throw new NotSupportedException("The Pyroscope fork does not include the Datadog tracer.");
        }

        internal void SetVariable(string key, string value)
        {
            CustomEnvironmentVariables[key] = value;
        }

        internal string GetLibraryExtension()
        {
            var profilerFileNameExt = GetOS() switch
            {
                "win" => "dll",
                "linux" => "so",
                _ => throw new PlatformNotSupportedException()
            };

            return profilerFileNameExt;
        }

        internal string GetProfilerNativeLibraryPath()
        {
            var configuredPath = System.Environment.GetEnvironmentVariable("PYROSCOPE_PROFILER_PATH");
            return string.IsNullOrWhiteSpace(configuredPath)
                ? Path.Combine(GetDeployDir(), $"Pyroscope.Profiler.Native.{GetLibraryExtension()}")
                : configuredPath;
        }

        internal void PopulateEnvironmentVariables(StringDictionary environmentVariables, MockDatadogAgent agent, int profilingExportIntervalInSeconds, string serviceName)
        {
            var profilerPath = GetProfilerNativeLibraryPath();

            if (!File.Exists(profilerPath))
            {
                throw new Exception($"Unable to find profiler dll at {profilerPath}.");
            }

            const string profilerGuid = "{BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}";

            environmentVariables["CORECLR_ENABLE_PROFILING"] = "1";
            environmentVariables["CORECLR_PROFILER"] = profilerGuid;
            environmentVariables["CORECLR_PROFILER_PATH"] = profilerPath;

            if (_enableProfiler)
            {
                environmentVariables["DD_PROFILING_ENABLED"] = "1";
            }
            else
            {
                environmentVariables["DD_PROFILING_ENABLED"] = "0";
            }

            // Linux ARM64: native profiler requires DD_INTERNAL_PROFILING_ENABLED_ARM64; align managed enablement.
            environmentVariables["DD_INTERNAL_PROFILING_ENABLED_ARM64"] = "1";

            environmentVariables["DD_PROFILING_UPLOAD_PERIOD"] = profilingExportIntervalInSeconds.ToString();
            environmentVariables["DD_TRACE_DEBUG"] = "1";

            if (!IsRunningOnWindows())
            {
                environmentVariables["LD_PRELOAD"] = GetLinuxApiWrapperPath();
            }

            if (serviceName != null)
            {
                serviceName = serviceName.Trim();
                if (serviceName.Length > 0)
                {
                    environmentVariables["DD_SERVICE"] = serviceName;
                    environmentVariables["PYROSCOPE_APPLICATION_NAME"] = serviceName;
                }
            }

            if (agent != null)
            {
                ConfigureTransportVariables(environmentVariables, agent);
            }

            foreach (var key in CustomEnvironmentVariables.Keys)
            {
                environmentVariables[key] = CustomEnvironmentVariables[key];
            }
        }

        internal string GetTestOutputPath()
        {
            return _testOutputPath;
        }

        private static void DisableDefaultProfilers(EnvironmentHelper env)
        {
            env.SetVariable(EnvironmentVariables.WallTimeProfilerEnabled, "0");
            env.SetVariable(EnvironmentVariables.CpuProfilerEnabled, "0");
            env.SetVariable(EnvironmentVariables.GarbageCollectionProfilerEnabled, "0");
            env.SetVariable(EnvironmentVariables.ExceptionProfilerEnabled, "0");
            env.SetVariable(EnvironmentVariables.ContentionProfilerEnabled, "0");
            env.SetVariable(EnvironmentVariables.GcThreadsCpuTimeEnabled, "0");
            env.SetVariable(EnvironmentVariables.ThreadLifetimeEnabled, "0");
        }

        private static string BuildTestOutputPath(string framework)
        {
            // DD_TESTING_OUPUT_DIR is set by the CI
            var baseTestOutputDir = Environment.GetEnvironmentVariable("DD_TESTING_OUPUT_DIR") ?? Path.Combine(Path.GetTempPath(), "ProfilerTest");

            var testName = TestContext.Current.TestName ?? "UnknownTestClass.UnknownTestMethod";
            var testOutputPath = Path.Combine(testName.Split('.'));

            testOutputPath = Path.Combine(baseTestOutputDir, testOutputPath, framework);

            // needed only for local test to ensure that we do not have artifacts from previous runs
            if (Directory.Exists(testOutputPath))
            {
                Directory.Delete(testOutputPath, recursive: true);
            }

            return testOutputPath;
        }

        private void ConfigureTransportVariables(StringDictionary environmentVariables, MockDatadogAgent agent)
        {
            if (agent is not MockDatadogAgent.HttpAgent http)
            {
                throw new NotSupportedException("Only the Pyroscope HTTP Push API is supported.");
            }

            http.ProfilesDirectory = PprofDir;
            environmentVariables["PYROSCOPE_SERVER_ADDRESS"] = $"http://127.0.0.1:{http.Port}";
        }

        private static string GetDeployDir()
        {
            return Path.Combine(GetRootOutputDir(), "DDProf-Deploy", IsAlpine ? "linux-musl" : "linux");
        }

        private static string GetRootOutputDir()
        {
            return Path.Combine(GetSolutionDirectory(), "artifacts", "profiler-build");
        }

        /// <summary>
        /// Find the solution directory from anywhere in the hierarchy.
        /// </summary>
        /// <returns>The solution directory.</returns>
        private static string GetSolutionDirectory()
        {
            if (_solutionDirectory == null)
            {
                var startDirectory = Environment.CurrentDirectory;
                var currentDirectory = Directory.GetParent(startDirectory);
                const string searchItem = @"Datadog.Profiler.sln";

                while (true)
                {
                    var slnFile = currentDirectory.GetFiles(searchItem).SingleOrDefault();

                    if (slnFile != null)
                    {
                        break;
                    }

                    currentDirectory = currentDirectory.Parent;

                    if (currentDirectory == null || !currentDirectory.Exists)
                    {
                        throw new Exception($"Unable to find solution directory from: {startDirectory}");
                    }
                }

                _solutionDirectory = currentDirectory.FullName;
            }

            return _solutionDirectory;
        }

        private static string GetOS()
        {
            return RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "win" :
                   RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? "linux" :
                   string.Empty;
        }

        private string GetLinuxApiWrapperPath()
        {
            var configuredPath = System.Environment.GetEnvironmentVariable("PYROSCOPE_API_WRAPPER_PATH");
            return string.IsNullOrWhiteSpace(configuredPath)
                ? Path.Combine(GetDeployDir(), "Datadog.Linux.ApiWrapper.x64.so")
                : configuredPath;
        }

        private void InitializeLogAndPprofEnvironmentVariables()
        {
            var baseOutputDir = GetTestOutputPath();
            CustomEnvironmentVariables[EnvironmentVariables.ProfilingLogDir] = Path.Combine(baseOutputDir, "logs");
            CustomEnvironmentVariables[EnvironmentVariables.ProfilingPprofDir] = Path.Combine(baseOutputDir, "pprofs");
            Directory.CreateDirectory(CustomEnvironmentVariables[EnvironmentVariables.ProfilingLogDir]);
            Directory.CreateDirectory(CustomEnvironmentVariables[EnvironmentVariables.ProfilingPprofDir]);
        }

    }
}
