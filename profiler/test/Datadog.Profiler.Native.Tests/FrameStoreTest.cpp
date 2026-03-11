// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include "FrameStore.h"

#include <string>

// FrameStore can be constructed with nullptr arguments for testing fake IP paths.
// The fake IP path (instructionPointer <= MaxFakeIP) returns immediately without
// touching any CLR APIs.
class FrameStoreTest : public ::testing::Test
{
protected:
    FrameStore frameStore{nullptr, nullptr, nullptr, nullptr};
};

// --- Fake IP frame tests ---

TEST_F(FrameStoreTest, LockContentionFrameIsHumanReadable)
{
    auto [isManaged, frame] = frameStore.GetFrame(FrameStore::FakeLockContentionIP);
    EXPECT_TRUE(isManaged);
    EXPECT_EQ(frame.Frame, "lock-contention");
}

TEST_F(FrameStoreTest, AllocationFrameIsHumanReadable)
{
    auto [isManaged, frame] = frameStore.GetFrame(FrameStore::FakeAllocationIP);
    EXPECT_TRUE(isManaged);
    EXPECT_EQ(frame.Frame, "allocation");
}

TEST_F(FrameStoreTest, UnknownFakeFrameIsHumanReadable)
{
    auto [isManaged, frame] = frameStore.GetFrame(FrameStore::FakeUnknownIP);
    EXPECT_TRUE(isManaged);
    EXPECT_EQ(frame.Frame, "Unknown-Type.Unknown-Method");
}

TEST_F(FrameStoreTest, FakeFramesDoNotContainPipeDelimitedFormat)
{
    std::vector<uintptr_t> fakeIPs = {
        FrameStore::FakeUnknownIP,
        FrameStore::FakeLockContentionIP,
        FrameStore::FakeAllocationIP
    };

    for (auto ip : fakeIPs)
    {
        auto [_, frame] = frameStore.GetFrame(ip);
        std::string frameStr(frame.Frame);
        EXPECT_EQ(frameStr.find("|lm:"), std::string::npos)
            << "Frame for IP " << ip << " should not contain |lm: but got: " << frameStr;
        EXPECT_EQ(frameStr.find("|fn:"), std::string::npos)
            << "Frame for IP " << ip << " should not contain |fn: but got: " << frameStr;
        EXPECT_EQ(frameStr.find("|ns:"), std::string::npos)
            << "Frame for IP " << ip << " should not contain |ns: but got: " << frameStr;
        EXPECT_EQ(frameStr.find("|ct:"), std::string::npos)
            << "Frame for IP " << ip << " should not contain |ct: but got: " << frameStr;
        EXPECT_EQ(frameStr.find("|sg:"), std::string::npos)
            << "Frame for IP " << ip << " should not contain |sg: but got: " << frameStr;
    }
}

// --- FormatFrame tests ---
// Test the static frame formatting function with various combinations
// of namespace, type, generics, and method names.

TEST(FormatFrameTest, SimpleNamespaceTypeMethod)
{
    auto result = FrameStore::FormatFrame(
        "System.Collections.Generic", "List", "", "Add", "");
    EXPECT_EQ(result, "System.Collections.Generic!List.Add");
}

TEST(FormatFrameTest, WithClassGenerics)
{
    auto result = FrameStore::FormatFrame(
        "System.Collections.Generic", "List", "<System.String>", "Add", "");
    EXPECT_EQ(result, "System.Collections.Generic!List<System.String>.Add");
}

TEST(FormatFrameTest, WithMethodGenerics)
{
    auto result = FrameStore::FormatFrame(
        "System.Linq", "Enumerable", "", "Select", "<System.Int32>");
    EXPECT_EQ(result, "System.Linq!Enumerable.Select<System.Int32>");
}

TEST(FormatFrameTest, WithBothClassAndMethodGenerics)
{
    auto result = FrameStore::FormatFrame(
        "System.Collections.Generic", "Dictionary", "<System.String, System.Int32>",
        "TryGetValue", "<System.Object>");
    EXPECT_EQ(result, "System.Collections.Generic!Dictionary<System.String, System.Int32>.TryGetValue<System.Object>");
}

TEST(FormatFrameTest, EmptyNamespace)
{
    auto result = FrameStore::FormatFrame(
        "", "MyType", "", "DoWork", "");
    EXPECT_EQ(result, "MyType.DoWork");
}

TEST(FormatFrameTest, MultipleClassGenericParameters)
{
    auto result = FrameStore::FormatFrame(
        "System.Collections.Generic", "Dictionary", "<System.String, System.Int32>",
        "ContainsKey", "");
    EXPECT_EQ(result, "System.Collections.Generic!Dictionary<System.String, System.Int32>.ContainsKey");
}

TEST(FormatFrameTest, NestedNamespace)
{
    auto result = FrameStore::FormatFrame(
        "MyApp.Services.Internal", "OrderService", "", "FindNearestVehicle", "");
    EXPECT_EQ(result, "MyApp.Services.Internal!OrderService.FindNearestVehicle");
}

TEST(FormatFrameTest, UnknownTypeWithMethodGenerics)
{
    auto result = FrameStore::FormatFrame(
        "", "Unknown-Type", "", "SomeMethod", "<T0>");
    EXPECT_EQ(result, "Unknown-Type.SomeMethod<T0>");
}

TEST(FormatFrameTest, DoesNotContainPipeDelimitedMarkers)
{
    auto result = FrameStore::FormatFrame(
        "System.Threading", "Monitor", "", "Enter", "");
    EXPECT_EQ(result, "System.Threading!Monitor.Enter");
    EXPECT_EQ(result.find("|lm:"), std::string::npos);
    EXPECT_EQ(result.find("|ns:"), std::string::npos);
    EXPECT_EQ(result.find("|ct:"), std::string::npos);
    EXPECT_EQ(result.find("|fn:"), std::string::npos);
    EXPECT_EQ(result.find("|sg:"), std::string::npos);
}
