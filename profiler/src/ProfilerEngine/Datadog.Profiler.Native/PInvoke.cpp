// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "PInvoke.h"
#include "CorProfilerCallback.h"
#include "IClrLifetime.h"
#include "Log.h"
#include "DynamicTagSetStore.h"
#include "ManagedThreadList.h"
#include "ProfilerEngineStatus.h"
#include "PyroscopeVersion.h"
#include "ThreadsCpuManager.h"

extern "C" void __stdcall ThreadsCpuManager_Map(std::uint32_t threadId, const WCHAR* pName)
{
    const auto profiler = CorProfilerCallback::GetInstance();
    if (profiler == nullptr)
    {
        Log::Error("ThreadsCpuManager_Map is called BEFORE CLR initialize");
        return;
    }

    profiler->GetThreadsCpuManager()->Map(threadId, pName);
}

extern "C" void* __stdcall GetNativeProfilerIsReadyPtr()
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("GetNativeProfilerIsReadyPtr is called BEFORE CLR initialize");
        return nullptr;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return nullptr;
    }

    return (void*)ProfilerEngineStatus::GetReadPtrIsProfilerEngineActive();
}

extern "C" const char* __stdcall GetPyroscopeProfilerVersion()
{
    return PYROSCOPE_SPY_VERSION;
}

extern "C" void* __stdcall GetPointerToNativeTraceContext()
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("GetPointerToNativeTraceContext is called BEFORE CLR initialize");
        return nullptr;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return nullptr;
    }

    profiler->TraceContextHasBeenSet();

    // Engine is active. Get info for current thread.
    auto pCurrentThreadInfo = ManagedThreadInfo::CurrentThreadInfo;
    if (pCurrentThreadInfo == nullptr)
    {
        // There was an error looking up the current thread info:
        return nullptr;
    }

    profiler->GetCodeHotspotThreadList()->RegisterThread(pCurrentThreadInfo);

    // Get pointers to the relevant fields within the thread info data structure.
    return pCurrentThreadInfo->GetTraceContextPointer();
}

extern "C" void __stdcall SetApplicationInfoForAppDomain(const char* runtimeId, const char* serviceName, const char* environment, const char* version)
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetApplicationInfo is called BEFORE profiler is created");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        Log::Error("SetApplicationInfo is called BEFORE CLR initialize");
        return;
    }

    profiler->GetApplicationStore()->SetApplicationInfo(
        runtimeId ? runtimeId : std::string(),
        serviceName ? serviceName : std::string(),
        environment ? environment : std::string(),
        version ? version : std::string(),
        std::string()); // process tags are only configured via the shared config for now.
}

extern "C" void __stdcall SetEndpointForTrace(const char* runtimeId, uint64_t traceId, const char* endpoint)
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetEndpointForTrace is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }

    static bool firstEmptyRuntimeId = true;
    if (runtimeId == nullptr)
    {
        if (firstEmptyRuntimeId)
        {
            Log::Error("SetEndpointForTrace was called with an empty runtime id");
            firstEmptyRuntimeId = false;
        }
        return;
    }

    static bool firstEmptyEndpoint = true;
    if (endpoint == nullptr)
    {
        if (firstEmptyEndpoint)
        {
            // It could happen that the endpoint is empty, but the tracer should check before making the call,
            // to avoid the cost of the p/invoke
            Log::Warn("SetEndpointForTrace was called with an empty endpoint");
            firstEmptyEndpoint = false;
        }
        return;
    }

    profiler->GetExporter()->SetEndpoint(runtimeId, traceId, endpoint);
}

extern "C" void __stdcall SetGitMetadataForApplication(const char* runtimeId, const char* repositoryUrl, const char* commitSha)
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetGitMetadataForApplication is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }

    static bool firstEmptyRuntimeId = true;
    if (runtimeId == nullptr)
    {
        if (firstEmptyRuntimeId)
        {
            Log::Error("SetGitMetadataForApplication was called with an empty runtime id");
            firstEmptyRuntimeId = false;
        }
        return;
    }

    profiler->GetApplicationStore()->SetGitMetadata(
        runtimeId,
        repositoryUrl != nullptr ? repositoryUrl : std::string(),
        commitSha != nullptr ? commitSha : std::string()
    );
}

extern "C" void __stdcall FlushProfile()
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("FlushProfile is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }

    Log::Debug("FlushProfile called by Managed code. Not implemented in Pyroscope.");
    //profiler->GetSamplesCollector()->Export();
}

extern "C" void __stdcall SetDynamicTag(const char* key, const char* value)
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("GetPointerToNativeTraceContext is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }

    // Engine is active. Get info for current thread.
    std::shared_ptr<ManagedThreadInfo> pCurrentThreadInfo{};
    HRESULT hr = profiler->GetManagedThreadList()->TryGetCurrentThreadInfo(pCurrentThreadInfo);
    if (FAILED(hr))
    {
        // There was an error looking up the current thread info:
        return;
    }

    pCurrentThreadInfo->GetTags()
        .Set(key, google::javaprofiler::AsyncRefCountedString(value));
}

extern "C" void __stdcall ClearDynamicTags()
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("GetPointerToNativeTraceContext is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }

    // Engine is active. Get info for current thread.
    std::shared_ptr<ManagedThreadInfo> pCurrentThreadInfo{};
    HRESULT hr = profiler->GetManagedThreadList()->TryGetCurrentThreadInfo(pCurrentThreadInfo);
    if (FAILED(hr))
    {
        // There was an error looking up the current thread info:
        return;
    }
    pCurrentThreadInfo->GetTags()
        .ClearAll();
}

extern "C" std::uint32_t __stdcall PushAsyncScope(std::uint32_t parentScopeId, const char* name)
{
    if (name == nullptr)
    {
        return parentScopeId;
    }

    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr || !profiler->GetClrLifetime()->IsInitialized())
    {
        return 0;
    }

    auto* const store = profiler->GetAsyncScopeStore();
    if (store == nullptr)
    {
        // Stitching disabled: report "no scope" so the managed side stops paying
        // for scope bookkeeping.
        return 0;
    }

    return store->Push(parentScopeId, name);
}

extern "C" std::uint32_t __stdcall InternDynamicTagSet(const char* const* keys, const char* const* values, std::int32_t count)
{
    if (keys == nullptr || values == nullptr || count <= 0)
    {
        return 0;
    }

    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr || !profiler->GetClrLifetime()->IsInitialized())
    {
        return 0;
    }

    auto* const store = profiler->GetDynamicTagSetStore();
    if (store == nullptr)
    {
        // Propagation disabled: tell the caller not to ask again, so it applies labels on the
        // thread that set them.
        return NoDynamicTagSetEver;
    }

    auto const id = store->Intern(keys, values, static_cast<std::size_t>(count));
    if (id == DynamicTagSetStore::NoTagSet)
    {
        // The store is full, or the set uses a key that cannot be registered. Neither
        // recovers, so let the caller stop paying for the attempt.
        return NoDynamicTagSetEver;
    }
    return id;
}

extern "C" void __stdcall SetCurrentProfilingContext(std::uint32_t asyncScopeId, std::uint32_t dynamicTagSetId)
{
    // The hot path: managed code calls this on every ExecutionContext switch of a flow that
    // carries a scope or labels. It reads the thread's own ManagedThreadInfo rather than going
    // through ManagedThreadList::TryGetCurrentThreadInfo, which takes a process-wide lock that
    // would serialize every continuation in the application. Reading the thread_local without
    // copying the shared_ptr is safe because we are on the owning thread, which is also the only
    // thread that clears it.
    auto* const pCurrentThreadInfo = ManagedThreadInfo::CurrentThreadInfo.get();
    if (pCurrentThreadInfo == nullptr)
    {
        // The CLR has not announced this thread to us yet, or already destroyed it, so there is
        // nothing that could be sampled.
        return;
    }

    pCurrentThreadInfo->SetAsyncScopeId(asyncScopeId);

    const auto profiler = CorProfilerCallback::GetInstance();
    if (profiler == nullptr)
    {
        return;
    }

    auto* const store = profiler->GetDynamicTagSetStore();
    if (store == nullptr)
    {
        // Propagation disabled. Leave the thread's tags alone: they are whatever the
        // thread-local SetDynamicTag API put there.
        return;
    }

    if (dynamicTagSetId == NoDynamicTagSetEver)
    {
        // A set the caller applies itself, key by key. Not ours to overwrite.
        return;
    }

    store->ApplyTo(dynamicTagSetId, pCurrentThreadInfo->GetTags());
}

extern "C" void __stdcall SetCPUTrackingEnabled(bool enabled)
{
    auto* const profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetStackSamplerEnabled is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }
    profiler->SetStackSamplerEnabled(enabled);
}

extern "C" void __stdcall SetAllocationTrackingEnabled(bool enabled)
{
    auto* const profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetAllocationTrackingEnabled is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }
    profiler->SetAllocationTrackingEnabled(enabled);
}

extern "C" void __stdcall SetContentionTrackingEnabled(bool enabled)
{
    auto* const profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetContentionTrackingEnabled is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }
    profiler->SetContentionTrackingEnabled(enabled);
}

extern "C" void __stdcall SetExceptionTrackingEnabled(bool enabled)
{
    auto* const profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetExceptionTrackingEnabled is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }
    profiler->SetExceptionTrackingEnabled(enabled);
}

extern "C" void __stdcall SetPyroscopeBasicAuth(const char* username, const char* password)
{
    auto* const profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetBasicAuth is called BEFORE CLR initialize");
        return;
    }

    if (!profiler->GetClrLifetime()->IsInitialized())
    {
        return;
    }
    auto sink = profiler->GetPyroscopePprofSink();
    if (!sink)
    {
        return;
    }
    sink->SetBasicAuth(BasicAuth{username, password});
}

extern "C" bool __stdcall SetConfiguration(shared::StableConfig::SharedConfig config)
{
    const auto profiler = CorProfilerCallback::GetInstance();

    if (profiler == nullptr)
    {
        Log::Error("SetConfiguration is called BEFORE CLR initialize");
        return false;
    }

    Log::Debug("SetConfiguration called by Managed code");

    return profiler->SetConfiguration(config);
}