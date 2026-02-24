// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "SampleValueTypeProvider.h"

#include <cassert>
#include <tuple>


const std::vector<SampleValueType> SampleValueTypeProvider::CpuTimeDefinitions = {
    {"cpu", "nanoseconds", ProfileType::ProcessCpu, -1},
    {"cpu_samples", "count", ProfileType::ProcessCpu, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::WallTimeDefinitions = {
    {"wall", "nanoseconds", ProfileType::WallTime, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::AllocDefinitions = {
    {"alloc_samples", "count", ProfileType::Alloc, -1},
    {"alloc_size", "bytes", ProfileType::Alloc, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::AllocFrameworkDefinitions = {
    {"alloc_samples", "count", ProfileType::AllocFramework, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::ContentionDefinitions = {
    {"lock_count", "count", ProfileType::Lock, -1},
    {"lock_time", "nanoseconds", ProfileType::Lock, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::ExceptionDefinitions = {
    {"exception", "count", ProfileType::Exception, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::NetworkDefinitions = {
    {"request_time", "nanoseconds", ProfileType::Network, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::LiveObjectsDefinitions = {
    {"inuse_objects", "count", ProfileType::LiveObjects, -1},
    {"inuse_space", "bytes", ProfileType::LiveObjects, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::GarbageCollectionDefinitions = {
    {"timeline", "nanoseconds", ProfileType::GcCpu, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::StopTheWorldDefinitions = {
    {"timeline", "nanoseconds", ProfileType::GcStw, -1}
};

const std::vector<SampleValueType> SampleValueTypeProvider::ThreadLifetimeDefinitions = {
    {"timeline", "nanoseconds", ProfileType::ThreadLifetime, -1}
};


SampleValueTypeProvider::SampleValueTypeProvider()
{
    _sampleTypeDefinitions.reserve(16);
}

std::vector<SampleValueTypeProvider::Offset> SampleValueTypeProvider::GetOrRegister(std::vector<SampleValueType> const& valueTypes)
{
    std::vector<Offset> offsets;
    offsets.reserve(valueTypes.size());
    bool incrementIndex = false;

    for (auto const& valueType : valueTypes)
    {
        size_t idx = GetOffset(valueType);
        if (idx == -1)
        {
            incrementIndex = true;
            SampleValueType registered = valueType;
            registered.Index = _nextIndex;

            idx = _sampleTypeDefinitions.size();
            _sampleTypeDefinitions.push_back(registered);
        }
        offsets.push_back(idx);
    }

    if (incrementIndex)
    {
        // the next set of SampleValueType will have a different index
        _nextIndex++;
    }

    return offsets;
}

std::vector<SampleValueType> const& SampleValueTypeProvider::GetValueTypes()
{
    return _sampleTypeDefinitions;
}

std::int8_t SampleValueTypeProvider::GetOffset(SampleValueType const& valueType)
{
    for (auto i = 0; i < _sampleTypeDefinitions.size(); i++)
    {
        auto const& current = _sampleTypeDefinitions[i];
        if (valueType.Name == current.Name && valueType.Type == current.Type)
        {
            if (valueType.Unit != current.Unit)
            {
                throw std::runtime_error("Cannot have the value type name with different unit: " + valueType.Unit + " != " + current.Unit);
            }
            return i;
        }
    }
    return -1;
}
