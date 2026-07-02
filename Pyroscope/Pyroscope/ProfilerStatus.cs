// <copyright file="ProfilerStatus.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>


using System.Runtime.InteropServices;

namespace Pyroscope
{
    internal class ProfilerStatus
    {
        private readonly bool _isProfilingEnabled;
        private readonly object _lockObj;
        private bool _isInitialized;
        private bool _isNativeVersionCompatible;
        private IntPtr _engineStatusPtr;

        public ProfilerStatus()
        {
            var isSupported = (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ||
                               RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) &&
                              RuntimeInformation.ProcessArchitecture == Architecture.X64;

            _isProfilingEnabled =
                (EnvironmentHelpers.GetEnvironmentVariable("DD_PROFILING_ENABLED")?.ToBoolean() ?? false) &&
                isSupported;


            _lockObj = new object();
            _isInitialized = false;
            _isNativeVersionCompatible = false;
        }

        public bool IsNativeVersionCompatible
        {
            get
            {
                EnsureNativeIsInitialized();
                return _isNativeVersionCompatible;
            }
        }

        public bool IsProfilerReady
        {
            get
            {
                if (!_isProfilingEnabled)
                {
                    return false;
                }

                EnsureNativeIsInitialized();
                return _isNativeVersionCompatible && _engineStatusPtr != IntPtr.Zero && Marshal.ReadByte(_engineStatusPtr) != 0;
            }
        }

        private void EnsureNativeIsInitialized()
        {
            if (_isInitialized)
            {
                return;
            }

            lock (_lockObj)
            {
                if (_isInitialized)
                {
                    return;
                }

                _isInitialized = true;

                try
                {
                    var managedVersion = ProfilerVersion.ManagedVersion;
                    var nativeVersion = NativeInterop.GetProfilerVersion();
                    if (!ProfilerVersion.IsCompatible(managedVersion, nativeVersion))
                    {
                        _isNativeVersionCompatible = false;
                        _engineStatusPtr = IntPtr.Zero;
                        return;
                    }

                    _isNativeVersionCompatible = true;
                    _engineStatusPtr = NativeInterop.GetProfilerStatusPointer();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[ProfilerStatus] Failed to get profiler status pointer: {ex.Message}");
                    _isNativeVersionCompatible = false;
                    _engineStatusPtr = IntPtr.Zero;
                }
            }
        }
    }
}
