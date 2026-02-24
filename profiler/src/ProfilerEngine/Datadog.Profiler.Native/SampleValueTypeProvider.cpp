// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "SampleValueTypeProvider.h"


SampleValueTypeProvider::SampleValueTypeProvider() :
    CpuTimeDefinitions{
        {"cpu", "nanoseconds", ProfileType::ProcessCpu, -1},
        {"cpu_samples", "count", ProfileType::ProcessCpu, -1}
    },
    WallTimeDefinitions{
        {"wall", "nanoseconds", ProfileType::WallTime, -1}
    },
    AllocDefinitions{
        {"alloc_samples", "count", ProfileType::Alloc, -1},
        {"alloc_size", "bytes", ProfileType::Alloc, -1}
    },
    AllocFrameworkDefinitions{
        {"alloc_samples", "count", ProfileType::AllocFramework, -1}
    },
    ContentionDefinitions{
        {"lock_count", "count", ProfileType::Lock, -1},
        {"lock_time", "nanoseconds", ProfileType::Lock, -1}
    },
    ExceptionDefinitions{
        {"exception", "count", ProfileType::Exception, -1}
    },
    NetworkDefinitions{
        {"request_time", "nanoseconds", ProfileType::Network, -1}
    },
    LiveObjectsDefinitions{
        {"inuse_objects", "count", ProfileType::LiveObjects, -1},
        {"inuse_space", "bytes", ProfileType::LiveObjects, -1}
    },
    GarbageCollectionDefinitions{
        {"gc_cpu_timeline", "nanoseconds", ProfileType::GcCpu, -1}
    },
    StopTheWorldDefinitions{
        {"gc_stw_timeline", "nanoseconds", ProfileType::GcStw, -1}
    },
    ThreadLifetimeDefinitions{
        {"thread_lifetime_timeline", "nanoseconds", ProfileType::ThreadLifetime, -1}
    }
{
    _sampleTypeDefinitions.reserve(16);
}

std::vector<SampleValueTypeProvider::Offset> SampleValueTypeProvider::GetOrRegister2(std::vector<SampleValueType>& valueTypes)
{
    std::vector<Offset> offsets;
    offsets.reserve(valueTypes.size());
    bool incrementIndex = false;

    for (auto& valueType : valueTypes)
    {
        size_t idx = GetOffset(valueType);
        if (idx == -1)
        {
            incrementIndex = true;
            // set the same index for all
            valueType.Index = _nextIndex;

            idx = _sampleTypeDefinitions.size();
            _sampleTypeDefinitions.push_back(valueType);
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
