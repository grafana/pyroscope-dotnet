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
        public static string GetProfilerVersion()
        {
            var versionPtr = NativeMethods.GetProfilerVersion();
            return versionPtr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(versionPtr) ?? string.Empty;
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
        public static uint PushAsyncScope(uint parentScopeId, string name)
        {
            return NativeMethods.PushAsyncScope(parentScopeId, name);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static uint InternDynamicTagSet(string[] keys, string[] values, int count)
        {
            // Marshalled by hand: the runtime rejects LPUTF8Str as an ArraySubType for string[]
            // (only LPStr/LPWStr/LPTStr are allowed), and LPStr would go through the platform ANSI
            // code page, which loses non-ASCII label values on Windows. IntPtr[] is blittable, so
            // the array itself is just pinned. This runs once per distinct label set, not per
            // sample, so the allocations are on a cold path.
            var keyPtrs = new IntPtr[count];
            var valuePtrs = new IntPtr[count];
            try
            {
                for (var i = 0; i < count; i++)
                {
                    keyPtrs[i] = Marshal.StringToCoTaskMemUTF8(keys[i]);
                    valuePtrs[i] = Marshal.StringToCoTaskMemUTF8(values[i]);
                }

                return NativeMethods.InternDynamicTagSet(keyPtrs, valuePtrs, count);
            }
            finally
            {
                for (var i = 0; i < count; i++)
                {
                    if (keyPtrs[i] != IntPtr.Zero)
                    {
                        Marshal.FreeCoTaskMem(keyPtrs[i]);
                    }

                    if (valuePtrs[i] != IntPtr.Zero)
                    {
                        Marshal.FreeCoTaskMem(valuePtrs[i]);
                    }
                }
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId)
        {
            NativeMethods.SetCurrentProfilingContext(asyncScopeId, dynamicTagSetId);
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

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "GetPyroscopeProfilerVersion")]
            public static extern IntPtr GetProfilerVersion();

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

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetPyroscopeBasicAuth")]
            public static extern void SetPyroscopeBasicAuth(string username, string password);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "PushAsyncScope")]
            public static extern uint PushAsyncScope(uint parentScopeId, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "InternDynamicTagSet")]
            public static extern uint InternDynamicTagSet(IntPtr[] keys, IntPtr[] values, int count);

            [DllImport(dllName: "Pyroscope.Profiler.Native", EntryPoint = "SetCurrentProfilingContext")]
            public static extern void SetCurrentProfilingContext(uint asyncScopeId, uint dynamicTagSetId);

        }
    }
}
