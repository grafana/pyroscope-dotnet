// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "SampleValueTypeProvider.h"

std::vector<SampleValueType> SampleValueTypeProvider::GetOrRegister(SampleProfileType profileType, std::vector<SampleValueType> const& valueTypes)
{
    (void) profileType;
    return valueTypes;
}
