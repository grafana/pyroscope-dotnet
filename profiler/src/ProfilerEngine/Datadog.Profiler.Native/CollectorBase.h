// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once
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
#include "IAppDomainStore.h"
#include "IRuntimeIdStore.h"
#include "IThreadsCpuManager.h"
#include "ProviderBase.h"
#include "RawSample.h"

#include "shared/src/native-src/string.h"

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
template <class TRawSample>   // TRawSample is supposed to inherit from RawSample
class CollectorBase
    :
    public IService,
    public ICollector<TRawSample>,  // allows profilers to add TRawSample instances
    public ProviderBase          // returns Samples to the aggregator
{
public:
    CollectorBase<TRawSample>(
        const char* name,
        uint32_t valueOffset,
        IThreadsCpuManager* pThreadsCpuManager,
        IFrameStore* pFrameStore,
        IAppDomainStore* pAppDomainStore,
        IRuntimeIdStore* pRuntimeIdStore,
        IConfiguration* pConfiguration
        ) :
        ProviderBase(name),
        _valueOffset{valueOffset},
        _pFrameStore{pFrameStore},
        _pAppDomainStore{pAppDomainStore},
        _pRuntimeIdStore{pRuntimeIdStore},
        _pThreadsCpuManager{pThreadsCpuManager},
        _isTimestampsAsLabelEnabled{pConfiguration->IsTimestampsAsLabelEnabled()}
    {
    }

// interfaces implementation
public:
    bool Start() override
    {
        return true;
    }

    bool Stop() override
    {
        return true;
    }

    const char* GetName() override
    {
        return _name.c_str();
    }

    void Add(TRawSample&& sample) override
    {
        std::lock_guard<std::mutex> lock(_rawSamplesLock);

        _collectedSamples.push_back(std::forward<TRawSample>(sample));
    }

    inline std::list<std::shared_ptr<Sample>> GetSamples() override
    {
        return TransformRawSamples(FetchRawSamples());
    }

    std::shared_ptr<Sample> TransformRawSample(const TRawSample& rawSample)
    {
        auto runtimeId = _pRuntimeIdStore->GetId(rawSample.AppDomainId);

        auto sample = std::make_shared<Sample>(rawSample.Timestamp, runtimeId == nullptr ? std::string_view() : std::string_view(runtimeId), rawSample.Stack.size());
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

        // add timestamp
        if (_isTimestampsAsLabelEnabled)
        {
            // All timestamps give the time when "something" ends and the associated duration
            // happened in the past
//            sample->AddNumericLabel(NumericLabel{Sample::EndTimestampLabel, sample->GetTimeStamp()});
        }

        // allow inherited classes to add values and specific labels
        rawSample.OnTransform(sample, _valueOffset);

        return sample;
    }

protected:
    uint64_t GetCurrentTimestamp()
    {
        return OpSysTools::GetHighPrecisionTimestamp();
    }

private:
    std::list<TRawSample> FetchRawSamples()
    {
        std::lock_guard<std::mutex> lock(_rawSamplesLock);

        std::list<TRawSample> input = std::move(_collectedSamples); // _collectedSamples is empty now
        return input;
    }

    std::list<std::shared_ptr<Sample>> TransformRawSamples(std::list<TRawSample>&& input)
    {
        std::list<std::shared_ptr<Sample>> samples;

        for (auto const& rawSample : input)
        {
            samples.push_back(TransformRawSample(rawSample));
        }

        return samples;
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
    uint32_t _valueOffset = 0;
    IFrameStore* _pFrameStore = nullptr;
    IAppDomainStore* _pAppDomainStore = nullptr;
    IRuntimeIdStore* _pRuntimeIdStore = nullptr;
    IThreadsCpuManager* _pThreadsCpuManager = nullptr;
    bool _isNativeFramesEnabled = false;
    bool _isTimestampsAsLabelEnabled = false;

    // A thread is responsible for asynchronously fetching raw samples from the input queue
    // and feeding the output sample list with symbolized frames and thread/appdomain names
    std::atomic<bool> _stopRequested = false;

    std::mutex _rawSamplesLock;
    std::list<TRawSample> _collectedSamples;
};
