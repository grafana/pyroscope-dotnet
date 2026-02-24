// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once
#include <cstdint>
#include "cor.h"
#include <memory>

#include "RawSample.h"

class RawWallTimeSample : public RawSample
{
public:
    inline static std::vector<SampleValueType> TypeDefinitions = {
        {"wall", "nanoseconds", ProfileType::Wall}
    };

    RawWallTimeSample() = default;

    RawWallTimeSample(RawWallTimeSample&& other) noexcept
        :
        RawSample(std::move(other)),
        Duration(other.Duration)
    {
    }

    RawWallTimeSample& operator=(RawWallTimeSample&& other) noexcept
    {
        if (this != &other)
        {
            RawSample::operator=(std::move(other));
            Duration = other.Duration;
        }
        return *this;
    }

    inline void OnTransform(std::shared_ptr<Sample>& sample) const override
    {
        sample->SetSampleValueTypes(&TypeDefinitions);
        sample->AddValue(Duration.count(), 0);
    }

    std::chrono::nanoseconds Duration;
};