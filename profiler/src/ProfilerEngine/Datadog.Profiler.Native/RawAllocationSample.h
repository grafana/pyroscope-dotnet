// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "RawSample.h"
#include "Sample.h"

class RawAllocationSample : public RawSample
{
public:
    RawAllocationSample() = default;

    RawAllocationSample(RawAllocationSample&& other) noexcept
        :
        RawSample(std::move(other)),
        AllocationClass(std::move(other.AllocationClass)),
        AllocationSize(other.AllocationSize),
        Address(other.Address),
        MethodTable(other.MethodTable)
    {
    }

    RawAllocationSample& operator=(RawAllocationSample&& other) noexcept
    {
        if (this != &other)
        {
            RawSample::operator=(std::move(other));
            AllocationClass = std::move(other.AllocationClass);
            AllocationSize = other.AllocationSize;
            Address = other.Address;
            MethodTable = other.MethodTable;
        }
        return *this;
    }

    inline void OnTransform(std::shared_ptr<Sample>& sample) const override
    {
        sample->AddValue(1, 0);

        // in .NET Framework, no size is available
        if (HasSize)
        {
            sample->AddValue(AllocationSize, 1);
        }
    }

    std::string AllocationClass;
    int64_t AllocationSize;
    bool HasSize = true;
    uintptr_t Address;
    ClassID MethodTable;
};