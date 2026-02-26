// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "SampleValueTypeProvider.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

static const SampleValueType CpuValueType = {"cpu", "nanoseconds", ProfileType::ProcessCpu, -1};
static const SampleValueType WallTimeValueType = {"walltime", "nanoseconds", ProfileType::WallTime, -1};
static const SampleValueType ExceptionValueType = {"exception", "count", ProfileType::Exception, -1};

testing::AssertionResult AreSampleValueTypeEqual(SampleValueType const& v1, SampleValueType const& v2)
{
    if (v1.Name == v2.Name && v1.Unit == v2.Unit)
        return testing::AssertionSuccess();
    else
        return testing::AssertionFailure() << " sample value type differs";
}

#define ASSERT_DEFINITIONS(expectedDefinitions, definitions)   \
    ASSERT_EQ(expectedDefinitions.size(), definitions.size()); \
    for (std::size_t i = 0; i < expectedDefinitions.size(); i++)      \
        ASSERT_PRED2(AreSampleValueTypeEqual, expectedDefinitions[i], definitions[i]);

#define ASSERT_OFFSETS(expectedOffsets, offsets)       \
    ASSERT_EQ(expectedOffsets.size(), offsets.size()); \
    for (std::size_t i = 0; i < expectedOffsets.size(); i++)  \
        ASSERT_EQ(expectedOffsets[i], offsets[i]);

using ValueOffsets = std::vector<SampleValueTypeProvider::Offset>;

TEST(SampleValueTypeProvider, RegisterValueTypes)
{
    SampleValueTypeProvider provider;
    auto valueTypes = std::vector<SampleValueType>{CpuValueType, WallTimeValueType};

    auto offsets = provider.RegisterPyroscopeSampleType(valueTypes);

    ASSERT_OFFSETS((ValueOffsets{0, 1}), offsets);
    //              cpu offset --^  ^-- walltime offset

    ASSERT_DEFINITIONS(valueTypes, provider.GetValueTypes());
}

TEST(SampleValueTypeProvider, RegisterMultipleValueTypes)
{
    SampleValueTypeProvider provider;
    auto valueTypes1 = std::vector<SampleValueType>{CpuValueType, WallTimeValueType};
    auto valueTypes2 = std::vector<SampleValueType>{ExceptionValueType};

    auto offsets1 = provider.RegisterPyroscopeSampleType(valueTypes1);
    auto offsets2 = provider.RegisterPyroscopeSampleType(valueTypes2);

    ASSERT_OFFSETS((ValueOffsets{0, 1}), offsets1);
    ASSERT_OFFSETS((ValueOffsets{2}), offsets2);

    auto allTypes = std::vector<SampleValueType>{CpuValueType, WallTimeValueType, ExceptionValueType};
    ASSERT_DEFINITIONS(allTypes, provider.GetValueTypes());
}

TEST(SampleValueTypeProvider, RegisterAssignsIndex)
{
    SampleValueTypeProvider provider;
    auto valueTypes1 = std::vector<SampleValueType>{CpuValueType, WallTimeValueType};
    auto valueTypes2 = std::vector<SampleValueType>{ExceptionValueType};

    provider.RegisterPyroscopeSampleType(valueTypes1);
    provider.RegisterPyroscopeSampleType(valueTypes2);

    // valueTypes1 should have index 0, valueTypes2 should have index 1
    ASSERT_EQ(0, valueTypes1[0].Index);
    ASSERT_EQ(0, valueTypes1[1].Index);
    ASSERT_EQ(1, valueTypes2[0].Index);
}