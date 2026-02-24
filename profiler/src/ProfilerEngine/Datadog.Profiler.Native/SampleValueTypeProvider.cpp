// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "SampleValueTypeProvider.h"


SampleValueTypeProvider::SampleValueTypeProvider() :
    CpuTimeDefinitions{
        {"cpu", "nanoseconds", ProfileType::ProcessCpu, -1},
        {"cpu_samples", "count", ProfileType::ProcessCpu, -1}
    },
    CpuSampleDefinitions{
        {"cpu", "nanoseconds", ProfileType::CpuSample, -1},
        {"cpu_samples", "count", ProfileType::CpuSample, -1}
    },
    GcThreadsCpuDefinitions{
        {"cpu", "nanoseconds", ProfileType::GcThreadsCpu, -1},
        {"cpu_samples", "count", ProfileType::GcThreadsCpu, -1}
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
        {"inuse_objects", "count", ProfileType::Heap, -1},
        {"inuse_space", "bytes", ProfileType::Heap, -1}
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

// Pyroscope patch: upstream deduplicates by name+type and shares a single Index across callers.
// We always register a fresh entry so each profiler gets its own independent index and offset.
std::vector<SampleValueTypeProvider::Offset> SampleValueTypeProvider::RegisterPyroscopeSampleType(std::vector<SampleValueType>& valueTypes)
{
    std::vector<Offset> offsets;
    offsets.reserve(valueTypes.size());

    for (auto& valueType : valueTypes)
    {
        // set the same index for all value types in this group
        valueType.Index = _nextIndex;

        size_t idx = _sampleTypeDefinitions.size();
        _sampleTypeDefinitions.push_back(valueType);
        offsets.push_back(idx);
    }

    // the next set of SampleValueType will have a different index
    _nextIndex++;

    return offsets;
}

std::vector<SampleValueType> const& SampleValueTypeProvider::GetValueTypes()
{
    return _sampleTypeDefinitions;
}

