// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "LiveObjectInfo.h"
#include "PoissonAllocationSampler.h"

std::atomic<uint64_t> LiveObjectInfo::s_nextObjectId = 1;


LiveObjectInfo::LiveObjectInfo(std::shared_ptr<Sample> sample, uintptr_t address,
                               uint64_t allocationSize, uint64_t allocationWindow, uint64_t samplingRate,
                               std::chrono::nanoseconds timestamp)
    :
    _address(address),
    _weakHandle(nullptr),
    _timestamp(timestamp),
    _gcCount(0),
    _allocationSize(allocationSize),
    _allocationWindow(allocationWindow),
    _samplingRate(samplingRate)
{
    _sample = sample;
}

void LiveObjectInfo::SetHandle(ObjectHandleID handle)
{
    _weakHandle = handle;
}

ObjectHandleID LiveObjectInfo::GetHandle() const
{
    return _weakHandle;
}

uintptr_t LiveObjectInfo::GetAddress() const
{
    return _address;
}

std::shared_ptr<Sample> LiveObjectInfo::GetSample() const
{
    return _sample;
}

void LiveObjectInfo::IncrementGC()
{
    _gcCount++;
}

bool LiveObjectInfo::IsGen2() const
{
    return _gcCount >= 2;
}

uint64_t LiveObjectInfo::GetAllocationSize() const
{
    return _allocationSize;
}

uint64_t LiveObjectInfo::GetAllocationWindow() const
{
    return _allocationWindow;
}

uint64_t LiveObjectInfo::GetSamplingRate() const
{
    return _samplingRate;
}

double LiveObjectInfo::GetUpscaleWeight() const
{
    // Layer 1: CLR's size-proportional AllocationTick sampling.
    // An object of size S in a 100KB window was the tick trigger with probability S/window.
    double w_clr = PoissonAllocationSampler::ComputeUpscaleWeight(_allocationSize, _allocationWindow);

    // Layer 2: our secondary Poisson filter that sub-samples AllocationTick events.
    double w_ours = PoissonAllocationSampler::ComputeUpscaleWeight(_allocationWindow, _samplingRate);

    return w_clr * w_ours;
}
