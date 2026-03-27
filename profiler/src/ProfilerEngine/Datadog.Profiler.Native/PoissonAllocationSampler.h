// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include <cstdint>
#include <cmath>
#include <mutex>
#include <random>

// Poisson-based allocation sampler inspired by Go's runtime heap profiler.
//
// Instead of sampling every Nth allocation (count-based), this sampler
// tracks bytes allocated and triggers a sample based on an exponential
// distribution with a configurable mean interval. This ensures that the
// probability of any given byte being the trigger is uniform (1/rate),
// making the sampling unbiased regardless of object size.
//
// Usage:
//   PoissonAllocationSampler sampler(524288);  // mean 512KB between samples
//   if (sampler.ShouldSample(objectSize)) { /* record sample */ }
//
// Upscaling: each sampled allocation should be weighted by:
//   weight = 1.0 / (1.0 - exp(-size / rate))
// to produce an unbiased estimate of the true heap.
class PoissonAllocationSampler
{
public:
    // meanSamplingInterval: average number of bytes between samples.
    // Go uses 524288 (512KB) by default.
    explicit PoissonAllocationSampler(uint64_t meanSamplingInterval);

    // Returns true if this allocation should be sampled.
    // allocationSize: the size of the allocated object in bytes.
    bool ShouldSample(uint64_t allocationSize);

    uint64_t GetMeanSamplingInterval() const;

    // Compute the upscale weight for a sampled allocation.
    // This is the inverse of the probability that an allocation of
    // the given size would have been sampled:
    //   weight = 1 / (1 - exp(-size / rate))
    static double ComputeUpscaleWeight(uint64_t allocationSize, uint64_t meanSamplingInterval);

private:
    uint64_t NextSamplingDistance();

    uint64_t _meanInterval;
    int64_t  _bytesUntilSample;

    std::mt19937_64 _rng;
    std::exponential_distribution<double> _expDist;
    std::mutex _mutex;
};
