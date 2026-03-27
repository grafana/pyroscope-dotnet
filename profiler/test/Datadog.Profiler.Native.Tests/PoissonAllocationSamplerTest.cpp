// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "PoissonAllocationSampler.h"

#include "gtest/gtest.h"

#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// ComputeUpscaleWeight
// ---------------------------------------------------------------------------

TEST(PoissonAllocationSamplerTest, UpscaleWeight_ZeroRate_ReturnsOne)
{
    // meanSamplingInterval == 0 is a degenerate case; the function must not
    // divide by zero and should return a neutral weight.
    double w = PoissonAllocationSampler::ComputeUpscaleWeight(1024, 0);
    EXPECT_DOUBLE_EQ(w, 1.0);
}

TEST(PoissonAllocationSamplerTest, UpscaleWeight_LargeAllocation_ApproachesOne)
{
    // When allocationSize >> meanSamplingInterval the probability of sampling
    // approaches 1, so the weight should be close to 1.
    uint64_t rate = 100u * 1024u; // 100 KB
    uint64_t size = 100u * rate;  // 100x the rate
    double w = PoissonAllocationSampler::ComputeUpscaleWeight(size, rate);
    EXPECT_NEAR(w, 1.0, 1e-6);
}

TEST(PoissonAllocationSamplerTest, UpscaleWeight_SmallAllocation_ApproachesRateOverSize)
{
    // For allocationSize << meanSamplingInterval, weight ≈ rate / size.
    uint64_t rate = 512u * 1024u; // 512 KB
    uint64_t size = 1u;
    double w = PoissonAllocationSampler::ComputeUpscaleWeight(size, rate);
    double expected = static_cast<double>(rate) / static_cast<double>(size);
    // Allow 1% relative error (Taylor expansion approximation).
    EXPECT_NEAR(w, expected, expected * 0.01);
}

TEST(PoissonAllocationSamplerTest, UpscaleWeight_EqualSizeAndRate_KnownValue)
{
    // weight = 1 / (1 - exp(-1)) ≈ 1.5820
    uint64_t rate = 100u * 1024u;
    double w = PoissonAllocationSampler::ComputeUpscaleWeight(rate, rate);
    double expected = 1.0 / (1.0 - std::exp(-1.0));
    EXPECT_NEAR(w, expected, 1e-9);
}

TEST(PoissonAllocationSamplerTest, UpscaleWeight_AlwaysAtLeastOne)
{
    uint64_t rate = 100u * 1024u;
    for (uint64_t size : {1u, 100u, 1024u, 100u * 1024u, 1024u * 1024u})
    {
        double w = PoissonAllocationSampler::ComputeUpscaleWeight(size, rate);
        EXPECT_GE(w, 1.0) << "weight < 1 for size=" << size;
    }
}

// ---------------------------------------------------------------------------
// ShouldSample — deterministic behaviour
// ---------------------------------------------------------------------------

TEST(PoissonAllocationSamplerTest, ShouldSample_EventuallyReturnsTrue)
{
    // With a finite mean interval, sampling must trigger within a reasonable
    // number of calls.
    PoissonAllocationSampler sampler(100u * 1024u);
    bool sampled = false;
    for (int i = 0; i < 10000; ++i)
    {
        if (sampler.ShouldSample(1024))
        {
            sampled = true;
            break;
        }
    }
    EXPECT_TRUE(sampled);
}

TEST(PoissonAllocationSamplerTest, ShouldSample_FixedWindowRate_CorrectFrequency)
{
    // When we pass exactly `rate` bytes per call, the sampler should fire
    // roughly once per call on average (since each call represents one mean
    // interval). Over many calls the sample rate should be close to 1.
    //
    // This models the < .NET 10 (AllocationTick) case where each call
    // represents a fixed 100 KB allocation window.
    const uint64_t rate = 100u * 1024u;
    PoissonAllocationSampler sampler(rate);

    int samples = 0;
    const int calls = 10000;
    for (int i = 0; i < calls; ++i)
    {
        if (sampler.ShouldSample(rate))
            samples++;
    }

    // Expect roughly one sample per call (Poisson mean = 1).
    // With 10 000 trials the std-dev ≈ 1, so a ±10% window is very safe.
    double ratio = static_cast<double>(samples) / calls;
    EXPECT_NEAR(ratio, 1.0, 0.10);
}

TEST(PoissonAllocationSamplerTest, ShouldSample_HalfRateWindow_HalfFrequency)
{
    // When each call represents half the mean interval, the sampler should
    // fire roughly once every two calls (sample rate ≈ 0.5).
    //
    // This models sub-sampling AllocationTick events at 2× the tick rate.
    const uint64_t rate = 200u * 1024u;
    const uint64_t window = 100u * 1024u;
    PoissonAllocationSampler sampler(rate);

    int samples = 0;
    const int calls = 10000;
    for (int i = 0; i < calls; ++i)
    {
        if (sampler.ShouldSample(window))
            samples++;
    }

    double ratio = static_cast<double>(samples) / calls;
    EXPECT_NEAR(ratio, 0.5, 0.05);
}

TEST(PoissonAllocationSamplerTest, ShouldSample_HighRateWindow_LowFrequency)
{
    // When each call represents a window much smaller than the mean interval,
    // the sample rate should be proportionally low.
    const uint64_t rate = 1000u * 1024u;
    const uint64_t window = 100u * 1024u; // 10× smaller
    PoissonAllocationSampler sampler(rate);

    int samples = 0;
    const int calls = 10000;
    for (int i = 0; i < calls; ++i)
    {
        if (sampler.ShouldSample(window))
            samples++;
    }

    double ratio = static_cast<double>(samples) / calls;
    EXPECT_NEAR(ratio, 0.1, 0.02);
}

// ---------------------------------------------------------------------------
// Two-layer upscale weight product
// ---------------------------------------------------------------------------

TEST(PoissonAllocationSamplerTest, TwoLayerWeight_ProductIsCorrect)
{
    // For live-heap profiling we apply:
    //   w_clr  = ComputeUpscaleWeight(objectSize, allocationWindow)
    //   w_ours = ComputeUpscaleWeight(allocationWindow, heapSamplingRate)
    //   weight = w_clr * w_ours
    //
    // Verify the product matches independently computed values.
    const uint64_t objectSize       = 4096u;
    const uint64_t allocationWindow = 100u * 1024u;
    const uint64_t heapSamplingRate = 512u * 1024u;

    double w_clr  = PoissonAllocationSampler::ComputeUpscaleWeight(objectSize, allocationWindow);
    double w_ours = PoissonAllocationSampler::ComputeUpscaleWeight(allocationWindow, heapSamplingRate);
    double weight = w_clr * w_ours;

    double expected_clr  = 1.0 / (1.0 - std::exp(-static_cast<double>(objectSize) / allocationWindow));
    double expected_ours = 1.0 / (1.0 - std::exp(-static_cast<double>(allocationWindow) / heapSamplingRate));

    EXPECT_NEAR(w_clr,  expected_clr,  1e-9);
    EXPECT_NEAR(w_ours, expected_ours, 1e-9);
    EXPECT_NEAR(weight, expected_clr * expected_ours, 1e-9);
    EXPECT_GE(weight, 1.0);
}
