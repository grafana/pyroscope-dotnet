#include "PprofBuilder.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <map>
#include <string>

namespace
{
google::v1::Profile BuildProfileWithTraceContext(const TraceContext& traceContext)
{
    PprofBuilder builder({SampleValueType{"cpu", "nanoseconds", ProfileType::ProcessCpu, -1}});
    Sample sample{"runtime-id"};
    sample.SetTraceContext(traceContext);

    std::array<int64_t, 1> values{42};
    builder.AddSample(sample, values);

    ProfileTime start{};
    auto end = start + std::chrono::milliseconds{1};
    auto serialized = builder.Build(start, end);

    google::v1::Profile profile;
    EXPECT_TRUE(profile.ParseFromString(serialized));
    return profile;
}

std::map<std::string, std::string> GetStringLabels(const google::v1::Profile& profile, const google::v1::Sample& sample)
{
    std::map<std::string, std::string> labels;
    for (const auto& label : sample.label())
    {
        labels.emplace(profile.string_table(label.key()), profile.string_table(label.str()));
    }

    return labels;
}
}

TEST(PprofBuilderTest, AddSampleWithTraceContextEncodesTraceLabelsAsPprofStringLabels)
{
    auto profile = BuildProfileWithTraceContext(TraceContext{
        ._currentLocalRootSpanId = 0x0102030405060708,
        ._currentTraceIdHi = 0x1122334455667788,
        ._currentTraceIdLo = 0x99aabbccddeeff00,
    });

    ASSERT_EQ(profile.sample_size(), 1);
    auto labels = GetStringLabels(profile, profile.sample(0));

    auto spanId = labels.find("span_id");
    ASSERT_NE(spanId, labels.end());
    EXPECT_EQ(spanId->second, "0807060504030201");

    auto traceId = labels.find("span_name");
    ASSERT_NE(traceId, labels.end());
    EXPECT_EQ(traceId->second, "887766554433221100ffeeddccbbaa99");
}

TEST(PprofBuilderTest, AddSampleWithoutLocalRootSpanIdDoesNotEmitTraceContextLabels)
{
    auto profile = BuildProfileWithTraceContext(TraceContext{
        ._currentLocalRootSpanId = 0,
        ._currentTraceIdHi = 0x1122334455667788,
        ._currentTraceIdLo = 0x99aabbccddeeff00,
    });

    ASSERT_EQ(profile.sample_size(), 1);
    auto labels = GetStringLabels(profile, profile.sample(0));

    EXPECT_EQ(labels.find("span_id"), labels.end());
    EXPECT_EQ(labels.find("span_name"), labels.end());
}
