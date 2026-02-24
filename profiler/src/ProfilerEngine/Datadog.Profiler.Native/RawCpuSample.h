// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "RawSample.h"
#include "Sample.h"

#include <chrono>

class RawCpuSample : public RawSample
{
public:
    // Caller must set SampleValueTypes before transformation (shared by CpuTimeProvider and GC CPU providers)
    const std::vector<SampleValueType>* SampleValueTypes = nullptr;

    RawCpuSample() noexcept = default;

    RawCpuSample(RawCpuSample&& other) noexcept
        :
        RawSample(std::move(other)),
        SampleValueTypes(other.SampleValueTypes),
        Duration(other.Duration)
    {
    }

    RawCpuSample& operator=(RawCpuSample&& other) noexcept
    {
        if (this != &other)
        {
            RawSample::operator=(std::move(other));
            SampleValueTypes = other.SampleValueTypes;
            Duration = other.Duration;
        }
        return *this;
    }

    inline void OnTransform(std::shared_ptr<Sample>& sample) const override
    {
        sample->SetSampleValueTypes(SampleValueTypes);
        sample->AddValue(Duration.count(), 0);
        sample->AddValue(1, 1);
    }
 
    std::chrono::nanoseconds Duration;
};