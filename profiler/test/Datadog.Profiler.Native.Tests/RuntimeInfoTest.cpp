#include "RuntimeInfo.h"

#include "gtest/gtest.h"

TEST(RuntimeInfoTest, ReturnsSemanticRuntimeLabelsForDotnet)
{
    RuntimeInfo runtimeInfo(8, 0, false);

    EXPECT_EQ(runtimeInfo.GetRuntimeName(), ".NET");
    EXPECT_EQ(runtimeInfo.GetRuntimeVersion(), "8.0");
    EXPECT_EQ(runtimeInfo.GetClrString(), "core-8.0");
}

TEST(RuntimeInfoTest, ReturnsSemanticRuntimeLabelsForDotnetFramework)
{
    RuntimeInfo runtimeInfo(4, 0, true);
    runtimeInfo.SetMinorVersions(8, 9195, 0);

    EXPECT_EQ(runtimeInfo.GetRuntimeName(), ".NET Framework");
    EXPECT_EQ(runtimeInfo.GetRuntimeVersion(), "4.8.9195");
    EXPECT_EQ(runtimeInfo.GetClrString(), "framework-4.8.9195");
}

TEST(RuntimeInfoTest, IncludesRevisionWhenAvailable)
{
    RuntimeInfo runtimeInfo(4, 0, true);
    runtimeInfo.SetMinorVersions(8, 9195, 1);

    EXPECT_EQ(runtimeInfo.GetRuntimeVersion(), "4.8.9195.1");
}
