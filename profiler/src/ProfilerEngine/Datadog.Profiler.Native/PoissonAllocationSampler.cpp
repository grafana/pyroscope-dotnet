// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "PoissonAllocationSampler.h"

#include <algorithm>

PoissonAllocationSampler::PoissonAllocationSampler(uint64_t meanSamplingInterval)
    : _meanInterval(meanSamplingInterval),
      _expDist(1.0 / static_cast<double>(meanSamplingInterval))
{
    std::random_device rd;
    _rng = std::mt19937_64(rd());
    _bytesUntilSample = static_cast<int64_t>(NextSamplingDistance());
}

bool PoissonAllocationSampler::ShouldSample(uint64_t allocationSize)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _bytesUntilSample -= static_cast<int64_t>(allocationSize);
    if (_bytesUntilSample > 0)
    {
        return false;
    }

    _bytesUntilSample = static_cast<int64_t>(NextSamplingDistance());
    return true;
}

uint64_t PoissonAllocationSampler::GetMeanSamplingInterval() const
{
    return _meanInterval;
}

double PoissonAllocationSampler::ComputeUpscaleWeight(uint64_t allocationSize, uint64_t meanSamplingInterval)
{
    if (meanSamplingInterval == 0)
    {
        return 1.0;
    }

    // Probability that this allocation was sampled:
    //   p = 1 - exp(-size / rate)
    double ratio = static_cast<double>(allocationSize) / static_cast<double>(meanSamplingInterval);
    double p = 1.0 - std::exp(-ratio);

    if (p <= 0.0)
    {
        // For extremely small allocations relative to rate, avoid division by zero.
        // Fall back to rate/size which is the limit of 1/p as size->0.
        return static_cast<double>(meanSamplingInterval) / std::max(static_cast<double>(allocationSize), 1.0);
    }

    return 1.0 / p;
}

uint64_t PoissonAllocationSampler::NextSamplingDistance()
{
    auto distance = static_cast<uint64_t>(_expDist(_rng));
    return std::max<uint64_t>(distance, 1);
}
