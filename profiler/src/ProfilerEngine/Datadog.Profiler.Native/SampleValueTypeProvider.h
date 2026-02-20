// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#pragma once

#include "Sample.h"

#include <vector>

class SampleValueTypeProvider
{
public:
    SampleValueTypeProvider() = default;

    std::vector<SampleValueType> GetOrRegister(SampleProfileType profileType, std::vector<SampleValueType> const& valueTypes);
};
