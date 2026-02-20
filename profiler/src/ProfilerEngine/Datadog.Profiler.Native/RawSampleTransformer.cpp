// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "RawSampleTransformer.h"

#include "OpSysTools.h"
#include "IAppDomainStore.h"
#include "IFrameStore.h"
#include "IRuntimeIdStore.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <sstream>
#include <iomanip>

std::shared_ptr<Sample> RawSampleTransformer::Transform(const RawSample& rawSample, std::vector<SampleValueType> const& sampleTypes, SampleProfileType profileType)
{
    auto sample = std::make_shared<Sample>(rawSample.Timestamp, std::string_view(), rawSample.Stack.Size());
    Transform(rawSample, sample, sampleTypes, profileType);
    return sample;
}

void RawSampleTransformer::Transform(const RawSample& rawSample, std::shared_ptr<Sample>& sample, std::vector<SampleValueType> const& sampleTypes, SampleProfileType profileType)
{
    sample->Reset();

    auto runtimeId = _pRuntimeIdStore->GetId(rawSample.AppDomainId);

    sample->SetRuntimeId(runtimeId == nullptr ? std::string_view() : std::string_view(runtimeId));
    sample->SetTimestamp(rawSample.Timestamp);
    sample->SetSampleTypes(sampleTypes);
    sample->SetProfileType(profileType);

    if (rawSample.LocalRootSpanId != 0)
    {
        std::stringstream profile_id;
        profile_id << std::hex << std::setw(16) << std::setfill('0') << rawSample.LocalRootSpanId;
        sample->AddLabel(StringLabel{Sample::ProfileIdLabel, profile_id.str()});//todo there is no need for refcounted string here
    }
    for (auto &tag: rawSample.Tags.GetAll()) {
        sample->AddLabel(StringLabel{tag.first, tag.second});
    }

    // compute thread/appdomain details
    // SetAppDomainDetails(rawSample, sample); // pyroscope removed because of spaces in the label name and cardinality
    // SetThreadDetails(rawSample, sample); // pyroscope removed because of spaces in the label name and cardinality

    // compute symbols for frames
    SetStack(rawSample, sample);

    // allow inherited classes to add values and specific labels
    rawSample.OnTransform(sample, sampleTypes);
}

void RawSampleTransformer::SetAppDomainDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
{
    auto appDomainName = _pAppDomainStore->GetName(rawSample.AppDomainId);
    sample->SetAppDomainName(std::string(appDomainName));
    sample->SetPid(OpSysTools::GetProcId());
}

void RawSampleTransformer::SetThreadDetails(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
{
    // needed for tests
    if (rawSample.ThreadInfo == nullptr)
    {
        // find a way to skip thread details like for garbage collection where no managed threads are involved
        // --> if everything is empty

        if (
            (rawSample.LocalRootSpanId == 0) &&
            (rawSample.SpanId == 0) &&
            (rawSample.AppDomainId == 0) &&
            (rawSample.Stack.Size() == 0))
        {
            sample->SetThreadId(std::string("GC"));
            sample->SetThreadName(std::string("CLR thread (garbage collector)"));
            return;
        }

        sample->SetThreadId(std::string("<0> [#0]"));
        sample->SetThreadName(std::string("Managed thread (name unknown) [#0]"));

        return;
    }

    sample->SetThreadId(rawSample.ThreadInfo->GetProfileThreadId());
    sample->SetThreadName(rawSample.ThreadInfo->GetProfileThreadName());
}

void RawSampleTransformer::SetStack(const RawSample& rawSample, std::shared_ptr<Sample>& sample)
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