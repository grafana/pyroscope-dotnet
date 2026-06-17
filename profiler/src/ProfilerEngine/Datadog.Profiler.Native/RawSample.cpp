// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "RawSample.h"

RawSample::RawSample() noexcept
    :
    Timestamp {0},
    AppDomainId {0},
    TraceContext{},
    ThreadInfo{nullptr},
    Stack{},
    Tags{}
{
}

RawSample::RawSample(RawSample&& other) noexcept
{
    *this = std::move(other);
}

RawSample& RawSample::operator=(RawSample&& other) noexcept
{
    Timestamp = other.Timestamp;
    AppDomainId = other.AppDomainId;
    TraceContext = other.TraceContext;
    ThreadInfo = std::move(other.ThreadInfo);
    Stack = std::move(other.Stack);
    Tags = std::move(other.Tags);

    return *this;
}