// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "RawSample.h"
#include "Sample.h"
#include "ManagedThreadInfo.h"

class RawContentionSample : public RawSample
{
public:
    inline static const std::string BucketLabelName = "Duration bucket";
    inline static const std::string WaitBucketLabelName = "Wait duration bucket";
    inline static const std::string RawCountLabelName = "raw count";
    inline static const std::string RawDurationLabelName = "raw duration";
    inline static const std::string BlockingThreadIdLabelName = "blocking thread id";
    inline static const std::string BlockingThreadNameLabelName = "blocking thread name";
    inline static const std::string ContentionTypeLabelName = "contention type";

public:
    inline static std::vector<SampleValueType> TypeDefinitions = {
        {"lock_count", "count", ProfileType::Lock},
        {"lock_time", "nanoseconds", ProfileType::Lock}
    };

    RawContentionSample()
    {
        SampleValueTypes = &TypeDefinitions;
    }

    RawContentionSample(RawContentionSample&& other) noexcept
        :
        RawSample(std::move(other)),
        ContentionDuration(other.ContentionDuration),
        Bucket(std::move(other.Bucket)),
        BlockingThreadId(other.BlockingThreadId),
        BlockingThreadName(std::move(other.BlockingThreadName)),
        Type(other.Type)
    {
    }

    RawContentionSample& operator=(RawContentionSample&& other) noexcept
    {
        if (this != &other)
        {
            RawSample::operator=(std::move(other));
            ContentionDuration = other.ContentionDuration;
            Bucket = std::move(other.Bucket);
            BlockingThreadId = other.BlockingThreadId;
            BlockingThreadName = std::move(other.BlockingThreadName);
            Type = other.Type;
        }
        return *this;
    }

    void OnTransform(std::shared_ptr<Sample>& sample) const override
    {
        sample->SetSampleValueTypes(SampleValueTypes);
        sample->AddValue(1, 0);
        sample->AddValue(ContentionDuration.count(), 1);
    }

    std::chrono::nanoseconds ContentionDuration;
    std::string Bucket;
    uint64_t BlockingThreadId;
    shared::WSTRING BlockingThreadName;
    ContentionType Type;

    static std::string ContentionTypes[static_cast<int>(ContentionType::ContentionTypeCount)];
};

inline std::string RawContentionSample::ContentionTypes[static_cast<int>(ContentionType::ContentionTypeCount)] =
{
    "Unknown",
    "Lock",
    "Wait",
};