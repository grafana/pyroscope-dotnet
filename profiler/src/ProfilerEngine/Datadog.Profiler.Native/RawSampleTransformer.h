// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.
#pragma once


#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "RawSample.h"
#include "Sample.h"

//forward declarations
class AsyncScopeStore;
class IAppDomainStore;
class IFrameStore;
class IRuntimeIdStore;

class RawSampleTransformer
{
public:
    // pAsyncScopeStore may be null: async stack stitching is then disabled and
    // samples keep only their physical frames.
    RawSampleTransformer(
        IFrameStore* pFrameStore,
        IAppDomainStore* pAppDomainStore,
        IRuntimeIdStore* pRuntimeIdStore,
        AsyncScopeStore* pAsyncScopeStore = nullptr) :
        _pFrameStore{pFrameStore},
        _pAppDomainStore{pAppDomainStore},
        _pRuntimeIdStore{pRuntimeIdStore},
        _pAsyncScopeStore{pAsyncScopeStore}
    {
    }

    ~RawSampleTransformer() = default;

    RawSampleTransformer(const RawSampleTransformer&) = delete;
    RawSampleTransformer& operator=(const RawSampleTransformer&) = delete;
    RawSampleTransformer(RawSampleTransformer&&) = delete;
    RawSampleTransformer& operator=(RawSampleTransformer&&) = delete;

    std::shared_ptr<Sample> Transform(const RawSample& rawSample, std::vector<SampleValueTypeProvider::Offset> const& offsets);

    // Stitch coverage, exported as metrics. All three stay at zero when stitching is disabled,
    // so an off switch does not read as a fault. Incremented once per sample on the transform
    // path and only ever read by the metrics registry, so relaxed ordering is enough.
    std::uint64_t GetStitchingSampleCount() const
    {
        return _stitchingSampleCount.load(std::memory_order_relaxed);
    }

    std::uint64_t GetStitchedSampleCount() const
    {
        return _stitchedSampleCount.load(std::memory_order_relaxed);
    }

    std::uint64_t GetStaleScopeIdCount() const
    {
        return _staleScopeIdCount.load(std::memory_order_relaxed);
    }

    void Transform(const RawSample& rawSample, std::shared_ptr<Sample>& sample, std::vector<SampleValueTypeProvider::Offset> const& offsets);

private:
    void SetAppDomainDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample);
    void SetThreadDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample);
    void SetStack(const RawSample& rawSample, std::shared_ptr<Sample>& sample);

    IFrameStore* _pFrameStore;
    IAppDomainStore* _pAppDomainStore;
    IRuntimeIdStore* _pRuntimeIdStore;
    AsyncScopeStore* _pAsyncScopeStore;

    std::atomic<std::uint64_t> _stitchingSampleCount{0};
    std::atomic<std::uint64_t> _stitchedSampleCount{0};
    std::atomic<std::uint64_t> _staleScopeIdCount{0};
};