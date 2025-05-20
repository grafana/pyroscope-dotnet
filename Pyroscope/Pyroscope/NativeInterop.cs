// <copyright file="NativeInterop.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2017 Datadog, Inc.
// </copyright>

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Pyroscope
{
    internal static class NativeInterop
    {
        [MethodImpl(MethodImplOptions.NoInlining)]
        public static IntPtr GetProfilerStatusPointer()
        {
            return NativeMethods.GetProfilerStatusPointer();
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static IntPtr GetTraceContextNativePointer()
        {
            return NativeMethods.GetTraceContextNativePointer();
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetApplicationInfoForAppDomain(string runtimeId, string serviceName, string environment, string version)
        {
            NativeMethods.SetApplicationInfoForAppDomain(runtimeId, serviceName, environment, version);
        }
        
        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetDynamicTag(string key, string value)
        {
            NativeMethods.SetDynamicTag(key, value);
        }
        
        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void ClearDynamicTags()
        {
            NativeMethods.ClearDynamicTags();
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetCPUTrackingEnabled(bool enabled)
        {
            NativeMethods.SetCPUTrackingEnabled(enabled);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetAllocationTrackingEnabled(bool enabled)
        {
            NativeMethods.SetAllocationTrackingEnabled(enabled);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetContentionTrackingEnabled(bool enabled)
        {
            NativeMethods.SetContentionTrackingEnabled(enabled);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetExceptionTrackingEnabled(bool enabled)
        {
            NativeMethods.SetExceptionTrackingEnabled(enabled);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetAuthToken(string authToken)
        {
            NativeMethods.SetPyroscopeAuthToken(authToken);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetBasicAuth(string username, string password)
        {
            NativeMethods.SetPyroscopeBasicAuth(username, password);
        }

        // These methods are rewritten by the native tracer to use the correct paths
        private static class NativeMethods
        {
            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "GetNativeProfilerIsReadyPtr")]
            public static extern IntPtr GetProfilerStatusPointer();

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "GetPointerToNativeTraceContext")]
            public static extern IntPtr GetTraceContextNativePointer();

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetApplicationInfoForAppDomain")]
            public static extern void SetApplicationInfoForAppDomain(string runtimeId, string serviceName, string environment, string version);
            
            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetDynamicTag")]
            public static extern void SetDynamicTag(string key, string value);
            
            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "ClearDynamicTags")]
            public static extern void ClearDynamicTags();

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetCPUTrackingEnabled")]
            public static extern void SetCPUTrackingEnabled(bool enabled);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetAllocationTrackingEnabled")]
            public static extern void SetAllocationTrackingEnabled(bool enabled);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetContentionTrackingEnabled")]
            public static extern void SetContentionTrackingEnabled(bool enabled);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetExceptionTrackingEnabled")]
            public static extern void SetExceptionTrackingEnabled(bool enabled);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetPyroscopeAuthToken")]
            public static extern void SetPyroscopeAuthToken(string authToken);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetPyroscopeBasicAuth")]
            public static extern void SetPyroscopeBasicAuth(string username, string password);

        }
    }
}
