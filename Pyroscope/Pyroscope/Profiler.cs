// <copyright file="Profiler.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

namespace Pyroscope
{
    public class Profiler
    {
        private static Profiler? _instance;

        internal Profiler(ContextTracker contextTracker)
        {
            _contextTracker = contextTracker;
        }

        public static Profiler Instance
        {
            get { return LazyInitializer.EnsureInitialized(ref _instance, Create); }
        }

        public void SetProfileId(ulong profileId)
        {
            _contextTracker.Set(profileId, 0);
        }

        public void SetDynamicTags(Dictionary<string, string> tags)
        {
            ClearDynamicTags();
            foreach (var (key, value) in tags)
            {
                SetDynamicTag(key, value);
            }
        }
        
        public void SetDynamicTag(string key, string value)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetDynamicTag(key, value);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }
        
        public void ClearDynamicTags()
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.ClearDynamicTags();
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        /// Enables or disables CPU/wall profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_CPU_ENABLED and
        /// PYROSCOPE_PROFILING_WALLTIME_ENABLED environment variables. If CPU/wall profiling is not
        /// configured, this function will have no effect.
        public void SetCPUTrackingEnabled(bool enabled)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetCPUTrackingEnabled(enabled);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        /// Enables or disables allocation profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_ALLOCATION_ENABLED environment variable.
        /// If allocation profiling is not configured, this function will have no effect.
        public void SetAllocationTrackingEnabled(bool enabled)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetAllocationTrackingEnabled(enabled);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        /// Enables or disables contention profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_CONTENTION_ENABLED environment variable.
        /// If contention profiling is not configured, this function will have no effect.
        public void SetContentionTrackingEnabled(bool enabled)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetContentionTrackingEnabled(enabled);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        /// Enables or disables exception profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_EXCEPTION_ENABLED environment variable.
        /// If exception profiling is not configured, this function will have no effect.
        public void SetExceptionTrackingEnabled(bool enabled)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetExceptionTrackingEnabled(enabled);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        public void SetAuthToken(string authToken)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetAuthToken(authToken);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

        public void SetBasicAuth(string username, string password)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetBasicAuth(username, password);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }
        
        private readonly ContextTracker _contextTracker;
        private bool _dllNotFound;

        private static Profiler Create()
        {
            var status = new ProfilerStatus();
            var contextTracker = new ContextTracker(status);
            return new Profiler(contextTracker);
        }
    }
}
