// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "IAppDomainStore.h"
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <iomanip>

#include "Log.h"
#include "OpSysTools.h"
#include "IService.h"
#include "ICollector.h"
#include "IConfiguration.h"
#include "IFrameStore.h"
#include "IRuntimeIdStore.h"
#include "IThreadsCpuManager.h"
#include "Log.h"
#include "OpSysTools.h"
#include "ProviderBase.h"
#include "RawSample.h"
#include "RawSamples.hpp"
#include "SamplesEnumerator.h"
#include "SampleValueTypeProvider.h"
#include "ServiceBase.h"

#include "shared/src/native-src/dd_memory_resource.hpp"
#include "shared/src/native-src/string.h"

#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// forward declarations
class IConfiguration;
class IFrameStore;
class IAppDomainStore;

using namespace std::chrono_literals;

// Base class used for storing raw samples that are transformed into exportable Sample
// every hundreds of milliseconds. The transformation code is setting :
//  - common labels (process id, thread id, appdomain, span ids) into Sample instances
//  - symbolized call stack (TODO: how to define a fake call stack? should we support
//    "hardcoded" fake IPs in the symbol store (0 = "Heap Profiler")?)
//
// Each profiler has to implement an inherited class responsible for setting its
// specific labels (such as exception name or exception message) if any but more important,
// to set its value(s) like wall time duration or cpu time duration.
//
template <class TRawSample> // TRawSample is supposed to inherit from RawSample
class CollectorBase
    :
    public ServiceBase,
    public ICollector<TRawSample>, // allows profilers to add TRawSample instances
    public ProviderBase            // returns Samples to the aggregator
{
public:
    CollectorBase<TRawSample>(
        const char* name,
        std::vector<SampleValueTypeProvider::Offset> valueOffsets,
        IThreadsCpuManager* pThreadsCpuManager,
        IFrameStore* pFrameStore,
        IAppDomainStore* pAppDomainStore,
        IRuntimeIdStore* pRuntimeIdStore,
        shared::pmr::memory_resource* memoryResource)
        :
        ProviderBase(name),
        _pFrameStore{pFrameStore},
        _pAppDomainStore{pAppDomainStore},
        _pRuntimeIdStore{pRuntimeIdStore},
        _pThreadsCpuManager{pThreadsCpuManager},
        _collectedSamples{memoryResource}
    {
        _valueOffsets = std::move(valueOffsets);
    }

    // interfaces implementation
public:

    const char* GetName() override
    {
        return _name.c_str();
    }

    void Add(TRawSample&& sample) override
    {
        _collectedSamples.Add(std::move(sample));
    }

    void TransformRawSample(const TRawSample& rawSample, std::shared_ptr<Sample>& sample)
    {
        sample->Reset();

        auto runtimeId = _pRuntimeIdStore->GetId(rawSample.AppDomainId);

        sample->SetRuntimeId(runtimeId == nullptr ? std::string_view() : std::string_view(runtimeId));
        sample->SetTimestamp(rawSample.Timestamp);

        if (rawSample.LocalRootSpanId != 0)
        {
            std::stringstream profile_id;
            profile_id << std::hex << std::setw(16) << std::setfill('0') << rawSample.LocalRootSpanId;
            sample->AddLabel(Label{Sample::ProfileIdLabel, profile_id.str()});//todo there is no need for refcounted string here
        }
        for (auto &tag: rawSample.Tags) {
            sample->AddLabel(Label{tag.first, tag.second});
        }

        // compute thread/appdomain details
        SetAppDomainDetails(rawSample, sample);
        SetThreadDetails(rawSample, sample);

        // compute symbols for frames
        SetStack(rawSample, sample);

        // allow inherited classes to add values and specific labels
        rawSample.OnTransform(sample, _valueOffsets);
    }

    // When TransformRawSample becomes a hot path, callers should call the overload
    // with std::shared_ptr<Sample> as out parameter (avoid alloc/dealloc and add up overhead)
    std::shared_ptr<Sample> TransformRawSample(const TRawSample& rawSample)
    {
        auto sample = std::make_shared<Sample>(0, std::string_view(), rawSample.Stack.Size());

        TransformRawSample(rawSample, sample);

        return sample;
    }

    std::unique_ptr<SamplesEnumerator> GetSamples() override
    {
        return std::make_unique<SamplesEnumeratorImpl>(_collectedSamples.Move(), this);
    }

protected:
    uint64_t GetCurrentTimestamp()
    {
        return OpSysTools::GetHighPrecisionTimestamp();
    }

    std::vector<SampleValueTypeProvider::Offset> const& GetValueOffsets() const
    {
        return _valueOffsets;
    }

private:

    class SamplesEnumeratorImpl : public SamplesEnumerator
    {
    public:
        SamplesEnumeratorImpl(RawSamples<TRawSample> rawSamples, CollectorBase<TRawSample>* collector) :
            _rawSamples{std::move(rawSamples)}, _collector{collector}, _currentRawSample{_rawSamples.begin()}
        {
        }

        // Inherited via SamplesEnumerator
        std::size_t size() const override
        {
            return _rawSamples.size();
        }

        bool MoveNext(std::shared_ptr<Sample>& sample) override
        {
            if (_currentRawSample == _rawSamples.end())
                return false;

            _collector->TransformRawSample(*_currentRawSample, sample);
            _currentRawSample++;

            return true;
        }

    private:
        RawSamples<TRawSample> _rawSamples;
        CollectorBase<TRawSample>* _collector;
        typename RawSamples<TRawSample>::iterator _currentRawSample;
    };

    bool StartImpl() override
    {
        return true;
    }

    bool StopImpl() override
    {
        return true;
    }

private:
    void SetAppDomainDetails(const TRawSample& rawSample, std::shared_ptr<Sample>& sample)
    {

    }

    void SetThreadDetails(const TRawSample& rawSample, std::shared_ptr<Sample>& sample)
    {

    }

    void SetStack(const TRawSample& rawSample, std::shared_ptr<Sample>& sample)
    {
        // Deal with fake stack frames like for garbage collections since the Stack will be empty
        for (auto const& instructionPointer : rawSample.Stack)
        {
            auto [isResolved, frame] = _pFrameStore->GetFrame(instructionPointer);

            if (isResolved)
            {
                sample->AddFrame(frame);
            }
        }
    }

private:
    IFrameStore* _pFrameStore = nullptr;
    IAppDomainStore* _pAppDomainStore = nullptr;
    IRuntimeIdStore* _pRuntimeIdStore = nullptr;
    IThreadsCpuManager* _pThreadsCpuManager = nullptr;
    bool _isNativeFramesEnabled = false;

    RawSamples<TRawSample> _collectedSamples;
    std::vector<SampleValueTypeProvider::Offset> _valueOffsets;
};
