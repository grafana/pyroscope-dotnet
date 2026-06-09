#include "gtest/gtest.h"

#include "PprofBuilder.h"
#include "google/v1/profile.pb.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace
{
const google::v1::Location* FindLocation(const google::v1::Profile& profile, uint64_t id)
{
    for (const auto& location : profile.location())
    {
        if (location.id() == id)
        {
            return &location;
        }
    }

    return nullptr;
}

const google::v1::Function* FindFunction(const google::v1::Profile& profile, uint64_t id)
{
    for (const auto& function : profile.function())
    {
        if (function.id() == id)
        {
            return &function;
        }
    }

    return nullptr;
}
} // namespace

TEST(PprofBuilderTest, PrefixesLeafFrameFunctionName)
{
    PprofBuilder builder({{"samples", "count", ProfileType::Alloc, 0}});

    Sample sample{"runtime-id"};
    sample.SetLeafFrame("My.Namespace.Type");

    std::array<int64_t, 1> values{1};
    builder.AddSample(sample, values);

    ProfileTime start{};
    ProfileTime end = start + std::chrono::milliseconds(1);
    const auto bytes = builder.Build(start, end);

    google::v1::Profile profile;
    ASSERT_TRUE(profile.ParseFromString(bytes));
    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_GE(profile.sample(0).location_id_size(), 1);

    const auto* leafLocation = FindLocation(profile, profile.sample(0).location_id(0));
    ASSERT_NE(leafLocation, nullptr);
    ASSERT_GE(leafLocation->line_size(), 1);

    const auto* leafFunction = FindFunction(profile, leafLocation->line(0).function_id());
    ASSERT_NE(leafFunction, nullptr);

    const auto nameIndex = leafFunction->name();
    ASSERT_GE(nameIndex, 0);
    ASSERT_LT(nameIndex, profile.string_table_size());
    EXPECT_EQ(profile.string_table(nameIndex), "cls!My.Namespace.Type");
}
