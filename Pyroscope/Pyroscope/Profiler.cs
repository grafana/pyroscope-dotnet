// <copyright file="Profiler.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

namespace Pyroscope
{
    public class Profiler
    {
        private static Profiler? _instance;

        internal Profiler(ProfilerStatus status, ContextTracker contextTracker, ProfilingContext profilingContext)
        {
            _status = status;
            _contextTracker = contextTracker;
            _profilingContext = profilingContext;
        }

        public static Profiler Instance
        {
            get { return LazyInitializer.EnsureInitialized(ref _instance, Create); }
        }

        [Obsolete("Use SetSpanContext(ulong localRootSpanId, ulong traceIdHi, ulong traceIdLo) instead.")]
        public void SetProfileId(ulong profileId)
        {
            _contextTracker.Set(profileId, 0, 0);
        }

        /// <summary>
        /// Makes this span the one the profiler attributes samples to, for the rest of the current
        /// async flow -- so work the span does after an <c>await</c> is attributed to it as well.
        /// </summary>
        public void SetSpanContext(ulong localRootSpanId, ulong traceIdHi, ulong traceIdLo)
        {
            _profilingContext.PushSpan(new SpanContext(localRootSpanId, traceIdHi, traceIdLo));
        }

        /// <summary>
        /// The ambient context that follows async flows: logical scopes (see
        /// <see cref="AsyncScope"/>), dynamic labels (see <see cref="LabelsWrapper"/>) and the
        /// active span. Exposed internally so those types and the tracing bridges share one
        /// context per process.
        /// </summary>
        internal ProfilingContext ProfilingContext
        {
            get { return _profilingContext; }
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
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetDynamicTag(key, value);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        public void ClearDynamicTags()
        {
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.ClearDynamicTags();
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
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
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetCPUTrackingEnabled(enabled);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        /// Enables or disables allocation profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_ALLOCATION_ENABLED environment variable.
        /// If allocation profiling is not configured, this function will have no effect.
        public void SetAllocationTrackingEnabled(bool enabled)
        {
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetAllocationTrackingEnabled(enabled);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        /// Enables or disables contention profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_CONTENTION_ENABLED environment variable.
        /// If contention profiling is not configured, this function will have no effect.
        public void SetContentionTrackingEnabled(bool enabled)
        {
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetContentionTrackingEnabled(enabled);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        /// Enables or disables exception profiling dynamically.
        ///
        /// This function works in conjunction with the PYROSCOPE_PROFILING_EXCEPTION_ENABLED environment variable.
        /// If exception profiling is not configured, this function will have no effect.
        public void SetExceptionTrackingEnabled(bool enabled)
        {
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetExceptionTrackingEnabled(enabled);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        public void SetBasicAuth(string username, string password)
        {
            if (!IsNativeInteropAvailable())
            {
                return;
            }

            try
            {
                NativeInterop.SetBasicAuth(username, password);
            }
            catch (DllNotFoundException ex)
            {
                DllNotFound(ex);
                _dllNotFound = true;
            }
        }

        private static void DllNotFound(DllNotFoundException ex)
        {
            Console.WriteLine(
                $"[Profiler] Failed to load Pyroscope.Profiler.Native.so : {ex}.\nConsider setting LD_LIBRARY_PATH pointing to the directory containing the Pyroscope.Profiler.Native.so");
        }

        private readonly ContextTracker _contextTracker;
        private readonly ProfilerStatus _status;
        private readonly ProfilingContext _profilingContext;
        private bool _dllNotFound;

        private static Profiler Create()
        {
            var status = new ProfilerStatus();
            var contextTracker = new ContextTracker(status);
            var profilingContext = new ProfilingContext(
                new NativeProfilingContextSink(contextTracker),
                stitchingEnabled: IsEnabled("PYROSCOPE_ASYNC_STITCHING_ENABLED"),
                propagationEnabled: IsEnabled("PYROSCOPE_ASYNC_CONTEXT_PROPAGATION_ENABLED"));
            return new Profiler(status, contextTracker, profilingContext);
        }

        // The same switches the native side reads, so one environment variable turns each
        // feature off end to end. Both default to on: they only do work once an application
        // (or a tracing bridge) actually opens a scope or sets labels.
        private static bool IsEnabled(string variable)
        {
            var value = EnvironmentHelpers.GetEnvironmentVariable(variable);
            return value == null || (value.ToBoolean() ?? true);
        }

        private bool IsNativeInteropAvailable()
        {
            return !_dllNotFound && _status.IsNativeVersionCompatible;
        }
    }
}
