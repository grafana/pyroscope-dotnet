// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "JavaProfilerTags.h"

#include <stdint.h>
#include <memory>
#include <vector>

#include "cor.h"
#include "corprof.h"

#include "Callstack.h"
#include "IThreadInfo.h"
#include "ManagedThreadInfo.h"
#include "SampleValueTypeProvider.h"

class Sample;

class RawSample
{
public:
    RawSample() noexcept;
    virtual ~RawSample() = default;

    RawSample(RawSample const&) = delete;
    RawSample& operator=(RawSample const&) = delete;

    RawSample(RawSample&& other) noexcept;
    RawSample& operator=(RawSample&& other) noexcept;

    // set values and additional labels on target sample
    virtual void OnTransform(std::shared_ptr<Sample>& sample, std::vector<SampleValueTypeProvider::Offset> const& valueOffset) const = 0;

public:
    std::chrono::nanoseconds Timestamp;
    AppDomainID AppDomainId;
    TraceContext TraceContext;
    std::shared_ptr<IThreadInfo> ThreadInfo;

    google::javaprofiler::Tags Tags;

    // Id of the logical async scope chain (see AsyncScopeStore) the sampled code
    // belongs to. RawSampleTransformer turns it into the frames that sit above the
    // physical stack, re-rooting an `await` continuation under its logical parent.
    std::uint32_t AsyncScopeId;

    // array of instruction pointers (32 or 64 bit address)
    Callstack Stack;
};
