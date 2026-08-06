#include "PprofBuilder.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

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

google::v1::Profile BuildProfileWithFrames(bool emitSourceLocation, const std::vector<FrameInfoView>& frames)
{
    PprofBuilder builder({SampleValueType{"cpu", "nanoseconds", ProfileType::ProcessCpu, -1}}, emitSourceLocation);
    Sample sample{"runtime-id"};
    sample.SetTraceContext(TraceContext{}); // Sample leaves it uninitialized: no trace labels wanted here
    for (auto const& frame : frames)
    {
        sample.AddFrame(frame);
    }

    std::array<int64_t, 1> values{42};
    builder.AddSample(sample, values);

    ProfileTime start{};
    auto end = start + std::chrono::milliseconds{1};
    auto serialized = builder.Build(start, end);

    google::v1::Profile profile;
    EXPECT_TRUE(profile.ParseFromString(serialized));
    return profile;
}

// Returns the function/line pair pprof associates with the location at the given index of the first sample.
std::pair<google::v1::Function, google::v1::Line> GetFrame(const google::v1::Profile& profile, int frameIndex)
{
    auto locationId = profile.sample(0).location_id(frameIndex);
    for (const auto& location : profile.location())
    {
        if (location.id() != locationId)
        {
            continue;
        }

        EXPECT_EQ(location.line_size(), 1);
        auto line = location.line(0);
        for (const auto& function : profile.function())
        {
            if (function.id() == line.function_id())
            {
                return {function, line};
            }
        }
    }

    ADD_FAILURE() << "no function found for frame " << frameIndex;
    return {};
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

    auto traceId = labels.find("trace_id");
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
    EXPECT_EQ(labels.find("trace_id"), labels.end());
}

TEST(PprofBuilderTest, WithoutSourceLocationTheFunctionFilenameIsTheModuleName)
{
    auto profile = BuildProfileWithFrames(false, {FrameInfoView{"MyApp", "MyApp.Worker.Run", "/src/MyApp/Worker.cs", 42}});

    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_EQ(profile.sample(0).location_id_size(), 1);
    auto [function, line] = GetFrame(profile, 0);

    EXPECT_EQ(profile.string_table(function.name()), "MyApp.Worker.Run");
    EXPECT_EQ(profile.string_table(function.filename()), "MyApp");
    EXPECT_EQ(function.start_line(), 0);
    EXPECT_EQ(line.line(), 0);
}

TEST(PprofBuilderTest, WithSourceLocationTheSourceFileAndStartLineAreEmitted)
{
    auto profile = BuildProfileWithFrames(true, {FrameInfoView{"MyApp", "MyApp.Worker.Run", "/src/MyApp/Worker.cs", 42},
                                                FrameInfoView{"MyLib", "MyLib.Queue.Pop", "/src/MyLib/Queue.cs", 7}});

    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_EQ(profile.sample(0).location_id_size(), 2);

    auto [worker, workerLine] = GetFrame(profile, 0);
    EXPECT_EQ(profile.string_table(worker.name()), "MyApp.Worker.Run");
    EXPECT_EQ(profile.string_table(worker.filename()), "/src/MyApp/Worker.cs");
    EXPECT_EQ(worker.start_line(), 42);
    EXPECT_EQ(workerLine.line(), 42);

    auto [queue, queueLine] = GetFrame(profile, 1);
    EXPECT_EQ(profile.string_table(queue.name()), "MyLib.Queue.Pop");
    EXPECT_EQ(profile.string_table(queue.filename()), "/src/MyLib/Queue.cs");
    EXPECT_EQ(queue.start_line(), 7);
    EXPECT_EQ(queueLine.line(), 7);
}

TEST(PprofBuilderTest, WithSourceLocationFramesWithoutDebugInfoFallBackToTheModuleName)
{
    // no .pdb was found for that method: Filename is empty and StartLine is 0
    auto profile = BuildProfileWithFrames(true, {FrameInfoView{"MyApp", "MyApp.Worker.Run", "", 0}});

    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_EQ(profile.sample(0).location_id_size(), 1);
    auto [function, line] = GetFrame(profile, 0);

    EXPECT_EQ(profile.string_table(function.filename()), "MyApp");
    EXPECT_EQ(function.start_line(), 0);
    EXPECT_EQ(line.line(), 0);
}

TEST(PprofBuilderTest, WithSourceLocationIdenticalFramesShareASingleLocation)
{
    FrameInfoView frame{"MyApp", "MyApp.Worker.Run", "/src/MyApp/Worker.cs", 42};
    auto profile = BuildProfileWithFrames(true, {frame, frame});

    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_EQ(profile.sample(0).location_id_size(), 2);
    EXPECT_EQ(profile.sample(0).location_id(0), profile.sample(0).location_id(1));
    EXPECT_EQ(profile.location_size(), 1);
    EXPECT_EQ(profile.function_size(), 1);
}

TEST(PprofBuilderTest, WithSourceLocationSameMethodInDifferentFilesGetsDistinctLocations)
{
    auto profile = BuildProfileWithFrames(true, {FrameInfoView{"MyApp", "MyApp.Worker.Run", "/src/MyApp/Worker.cs", 42},
                                                FrameInfoView{"MyApp", "MyApp.Worker.Run", "/src/MyApp/Worker.g.cs", 42}});

    ASSERT_EQ(profile.sample_size(), 1);
    ASSERT_EQ(profile.sample(0).location_id_size(), 2);
    EXPECT_NE(profile.sample(0).location_id(0), profile.sample(0).location_id(1));
    EXPECT_EQ(profile.location_size(), 2);
}
