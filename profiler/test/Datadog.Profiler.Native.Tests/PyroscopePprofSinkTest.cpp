// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include <map>
#include <string>

#include "PyroscopePprofSink.h"
#include "RuntimeInfo.h"
#include "dd_profiler_version.h"

namespace
{
std::map<std::string, std::string> GetLabels(const PyroscopePprofSink& sink, ProfileType profileType)
{
    std::map<std::string, std::string> labels;
    for (const auto& [name, value] : sink.GetLabels(profileType))
    {
        labels[name] = value;
    }
    return labels;
}

PyroscopePprofSink CreateSink(RuntimeInfo* runtimeInfo, const PyroscopePprofSink::Labels& staticTags)
{
    return PyroscopePprofSink("http://127.0.0.1:4040", "service", BasicAuth{}, PyroscopeTenantId{}, "", runtimeInfo, staticTags);
}
} // namespace

TEST(PyroscopePprofSinkTest, AddsSemanticSeriesLabels)
{
    RuntimeInfo runtimeInfo(8, 0, false);
    auto sink = CreateSink(&runtimeInfo, {{"static_label", "static_value"}});

    const auto labels = GetLabels(sink, ProfileType::ProcessCpu);
    EXPECT_EQ(labels.at("__name__"), "process_cpu");
    EXPECT_EQ(labels.at("service_name"), "service");
    EXPECT_EQ(labels.at("spy_name"), "dotnetspy");
    EXPECT_EQ(labels.at("otel.scope.name"), "com.grafana.pyroscope/dotnet");
    EXPECT_EQ(labels.at("otel.scope.version"), PROFILER_VERSION);
    EXPECT_EQ(labels.at("process.runtime.name"), ".NET");
    EXPECT_EQ(labels.at("process.runtime.version"), "8.0");
    EXPECT_EQ(labels.at("static_label"), "static_value");
}

TEST(PyroscopePprofSinkTest, DoesNotOverrideUserProvidedSemanticLabels)
{
    RuntimeInfo runtimeInfo(8, 0, false);
    auto sink = CreateSink(&runtimeInfo,
                           {
                               {"otel.scope.name", "user.scope"},
                               {"otel.scope.version", "user-version"},
                               {"process.runtime.name", "user-runtime"},
                               {"process.runtime.version", "user-runtime-version"},
                           });

    const auto labels = GetLabels(sink, ProfileType::ProcessCpu);
    EXPECT_EQ(labels.at("otel.scope.name"), "user.scope");
    EXPECT_EQ(labels.at("otel.scope.version"), "user-version");
    EXPECT_EQ(labels.at("process.runtime.name"), "user-runtime");
    EXPECT_EQ(labels.at("process.runtime.version"), "user-runtime-version");
}

TEST(PyroscopePprofSinkTest, UsesUpdatedDotnetFrameworkRuntimeVersion)
{
    RuntimeInfo runtimeInfo(4, 0, true);
    auto sink = CreateSink(&runtimeInfo, {});

    runtimeInfo.SetMinorVersions(8, 9195, 0);

    const auto labels = GetLabels(sink, ProfileType::ProcessCpu);
    EXPECT_EQ(labels.at("process.runtime.name"), ".NET Framework");
    EXPECT_EQ(labels.at("process.runtime.version"), "4.8.9195");
}

TEST(PyroscopePprofSinkTest, MapsProfileTypeToName)
{
    RuntimeInfo runtimeInfo(8, 0, false);
    auto sink = CreateSink(&runtimeInfo, {});

    EXPECT_EQ(GetLabels(sink, ProfileType::WallTime).at("__name__"), "wall");
    EXPECT_EQ(GetLabels(sink, ProfileType::Alloc).at("__name__"), "memory");
    EXPECT_EQ(GetLabels(sink, ProfileType::Lock).at("__name__"), "lock");
    EXPECT_EQ(GetLabels(sink, ProfileType::GcStw).at("__name__"), "gc");
}
