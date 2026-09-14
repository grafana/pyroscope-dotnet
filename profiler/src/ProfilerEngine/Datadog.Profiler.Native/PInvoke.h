// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "unknwn.h"
#include <cstdint>
#include <mutex>
#include <winerror.h>

#include "shared/src/native-src/stable_config.h"

/*
   TL;DR When returning a boolean value to the managed part, we must use a C BOOL type instead of C++ bool type.

   Boolean type has different size/representation in C (4 bytes), C++ (1 byte) and C# (1 byte).

   For example:
   [DllImport(dllName: NativeProfilerEngineLibName_x86, EntryPoint = "StackSnapshotsBufferManager_TryCompleteCurrentWriteSegment",
              CallingConvention = CallingConvention.StdCall)]
   private static extern bool StackSnapshotsBufferManager_TryCompleteCurrentWriteSegment_x86();

   If not specified otherwise, the marshaller will consider the boolean type as a Win32 BOOL type (4 bytes). It will read the register RAX/EAX
   and convert its content to the appropriate C# bool value (false == 0, true otherwise).

   On the native side:
   extern "C" bool __stdcall StackSnapshotsBufferManager_TryCompleteCurrentWriteSegment();

   In this case, the function return a C++ bool value (1 byte). So only the AL register will be set as the return value. However, he AL
   is lowest byte of the RAX/EAX register.
   This means that the RAX/EAX register will have only its first byte changed: the other bytes will remain unchanged with probably
   garbage definitely not 0.

   In this case, if StackSnapshotsBufferManager_TryCompleteCurrentWriteSegment returns false, the managed part may see the return value
   as true.

 */

extern "C" void* __stdcall GetNativeProfilerIsReadyPtr();

extern "C" const char* __stdcall GetPyroscopeProfilerVersion();

extern "C" void* __stdcall GetPointerToNativeTraceContext();

extern "C" void __stdcall SetApplicationInfoForAppDomain(const char* runtimeId, const char* serviceName, const char* environment, const char* version);

extern "C" void __stdcall FlushProfile();


extern "C" void __stdcall SetDynamicTag(const char* key, const char* value);
extern "C" void __stdcall ClearDynamicTags();
extern "C" void __stdcall SetCPUTrackingEnabled(bool enabled);
extern "C" void __stdcall SetAllocationTrackingEnabled(bool enabled);
extern "C" void __stdcall SetContentionTrackingEnabled(bool enabled);
extern "C" void __stdcall SetExceptionTrackingEnabled(bool enabled);
extern "C" void __stdcall SetPyroscopeBasicAuth(const char *user, const char *password);

// Profiler context that follows async flows (see AsyncScopeStore, DynamicTagSetStore).
//
// The managed side keeps the current async scope chain and label set in AsyncLocals, so they
// survive `await`, and re-publishes them onto every thread the ExecutionContext lands on.
// Both are interned to an id first, so the per-continuation call is a single pair of
// integers rather than a string marshalling exercise.
//
// PushAsyncScope interns `name` under `parentScopeId` and returns the new chain's id.
// InternDynamicTagSet interns `count` (key, value) pairs and returns the set's id.
//
// 0 means "none", and is also what both return while the profiler is still starting up, so
// the caller should try again later. InternDynamicTagSet returns NoDynamicTagSetEver when
// the set can *never* be interned -- propagation is switched off, the store is full, or the
// process has run out of the 16 label keys Tags supports -- so the caller can stop asking
// and apply those labels the old way, on the thread that set them.
constexpr std::uint32_t NoDynamicTagSetEver = 0xFFFFFFFFu;
extern "C" std::uint32_t __stdcall PushAsyncScope(std::uint32_t parentScopeId, const char* name);
extern "C" std::uint32_t __stdcall InternDynamicTagSet(const char* const* keys, const char* const* values, std::int32_t count);
extern "C" void __stdcall SetCurrentProfilingContext(std::uint32_t asyncScopeId, std::uint32_t dynamicTagSetId);


extern "C" bool __stdcall SetConfiguration(shared::StableConfig::SharedConfig config);