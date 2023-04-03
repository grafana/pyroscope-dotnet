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

        public void SetStackSamplerEnabled(bool enabled)
        {
            if (_dllNotFound)
            {
                return;
            }
            try
            {
                NativeInterop.SetStackSamplerEnabled(enabled);
            }
            catch (DllNotFoundException)
            {
                _dllNotFound = true;
            }
        }

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
