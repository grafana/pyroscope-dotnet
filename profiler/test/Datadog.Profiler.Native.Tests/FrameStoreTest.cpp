// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "FrameStore.h"
#include "ManagedCodeCache.h"
#include "MockProfilerInfo.h"

#include <memory>
#include <string>
#include <vector>

using namespace testing;

namespace {

// Kept in sync with the plain placeholder produced by FrameStore::GetFrame. Although
// unresolved native frames are normally filtered out, Windows SEH recovery paths keep
// this frame, so it must remain suitable for direct use by the pprof sink.
constexpr const char* NotResolvedFrameText = "NotResolvedFrame";

} // namespace

// ============================================================================
// Fake-IP frame tests (pyroscope fork)
//
// The pyroscope fork resolves synthetic fake-IP frames to plain, human-readable
// names instead of Datadog's pipe-delimited "|lm:...|fn:...|sg:(?)" encoding,
// because the pprof sink consumes the frame name directly.
//
// FrameStore can be constructed with nullptr arguments here: the fake IP path
// (instructionPointer <= MaxFakeIP) returns immediately without touching any CLR APIs.
// ============================================================================
class FrameStoreTest : public ::testing::Test
{
protected:
    FrameStore frameStore{nullptr, nullptr, nullptr, nullptr};
};

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

TEST_F(FrameStoreTest, UnknownFrameTypeFrameIsHumanReadable)
{
    // Synthetic frame introduced in dd-trace-dotnet v3.44.0 (UnknownFrameTypeIP). Its
    // resolved flag depends on debug logging, so only assert the fork-simplified name.
    auto [isManaged, frame] = frameStore.GetFrame(FrameStore::UnknownFrameTypeIP);
    (void)isManaged;
    EXPECT_EQ(frame.Frame, "Unknown-Frame-Type");
}

TEST_F(FrameStoreTest, FakeFramesDoNotContainPipeDelimitedFormat)
{
    std::vector<uintptr_t> fakeIPs = {
        FrameStore::FakeUnknownIP,
        FrameStore::FakeLockContentionIP,
        FrameStore::FakeAllocationIP,
        FrameStore::UnknownFrameTypeIP
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

// ============================================================================
// Native-IP resolution tests (carried over from dd-trace-dotnet v3.44.0)
//
// These guard the contract that FrameStore::GetFrame uses to tell
// RawSampleTransformer whether a frame is resolved (kept) or not (dropped):
//   - pair<true, ...>  => keep the frame in the sample
//   - pair<false, ...> => drop the frame from the sample
//
// Native instruction pointers (addresses that don't belong to any managed method)
// MUST be reported as "not resolved" (false) so that they are filtered out by the
// transformer. Regressing this behavior causes long runs of "NotResolvedFrame"
// entries at the top of exception/walltime profiles on platforms that unwind
// native frames before reaching managed frames (notably Linux ARM64, where the
// HybridUnwinder walks native frames via libunwind before transitioning to
// managed frames via frame-pointer unwinding).
//
// Two parallel code paths exist in FrameStore::GetFrame:
//   * no ManagedCodeCache: asks ICorProfilerInfo::GetFunctionFromIP directly
//   * with ManagedCodeCache: asks the cache for the FunctionID
// Both must behave the same way for native IPs.
//
// These use a distinct test suite name (FrameStoreNativeIpTest) so they can be
// declared with plain TEST(...) alongside the TEST_F fixture above; GoogleTest
// forbids mixing TEST and TEST_F within the same suite.
// ============================================================================

// No-cache path: a native IP (FAILED hr from GetFunctionFromIP) must be reported as
// not resolved so RawSampleTransformer drops it from the sample.
TEST(FrameStoreNativeIpTest, GetFrame_NoCache_NativeIp_ReturnsNotResolvedAndDropped)
{
    auto mockProfiler = MockProfilerInfo{};

    EXPECT_CALL(mockProfiler, GetFunctionFromIP(_, _))
        .WillRepeatedly(Return(E_FAIL));

    FrameStore frameStore(
        /*pCorProfilerInfo*/ &mockProfiler,
        /*pConfiguration  */ nullptr,
        /*pDebugInfoStore */ nullptr,
        /*pManagedCodeCache*/ nullptr);

    // IP must be greater than FrameStore::MaxFakeIP so we don't hit the fake-IP
    // short-circuit at the top of GetFrame.
    const uintptr_t nativeIp = 0x12345;

    auto [isResolved, frameInfo] = frameStore.GetFrame(nativeIp);

    EXPECT_FALSE(isResolved) << "Native IPs must be reported as unresolved so "
                                "RawSampleTransformer drops them from the sample.";
    EXPECT_EQ(std::string(frameInfo.Frame), std::string(NotResolvedFrameText));
}

// Cached path: a native IP that is not in any managed code range must be reported as
// not resolved. The ManagedCodeCache returns InvalidFunctionId for such IPs;
// FrameStore must translate that to isResolved == false.
//
// This is the regression guard for the ARM64 exception-profiling issue where a
// long sequence of "NotResolvedFrame" entries leaked into the sample because
// FrameStore was returning isResolved == true for InvalidFunctionId.
TEST(FrameStoreNativeIpTest, GetFrame_WithCache_NativeIp_ReturnsNotResolvedAndDropped)
{
    auto mockProfiler = MockProfilerInfo{};

    // Empty cache => any IP resolves to InvalidFunctionId (there are no registered
    // JIT ranges and no R2R modules), which is exactly the "native IP" case.
    auto cache = std::make_unique<ManagedCodeCache>(&mockProfiler);
    cache->Initialize();

    FrameStore frameStore(
        /*pCorProfilerInfo*/ &mockProfiler,
        /*pConfiguration  */ nullptr,
        /*pDebugInfoStore */ nullptr,
        /*pManagedCodeCache*/ cache.get());

    // Sanity-check the upstream contract we rely on: the cache must report the IP
    // as "definitely native" (a value equal to InvalidFunctionId), not nullopt.
    const uintptr_t nativeIp = 0xDEAD;
    auto cacheResult = cache->GetFunctionId(nativeIp);
    ASSERT_TRUE(cacheResult.has_value());
    ASSERT_EQ(ManagedCodeCache::InvalidFunctionId, cacheResult.value());

    auto [isResolved, frameInfo] = frameStore.GetFrame(nativeIp);

    EXPECT_FALSE(isResolved) << "Native IPs (InvalidFunctionId from the cache) must "
                                "be reported as unresolved so RawSampleTransformer "
                                "drops them from the sample.";
    EXPECT_EQ(std::string(frameInfo.Frame), std::string(NotResolvedFrameText));

    cache.reset();
}

#ifdef _WINDOWS
// Windows only: cached-path "nullopt" branch simulates the SEH mirror inside
// ManagedCodeCache::GetFunctionId. When ICorProfilerInfo::GetFunctionFromIP crashes
// (e.g. module unloaded concurrently) the __try/__except in the cache returns
// std::nullopt. FrameStore must translate that to {isResolved=true, NotResolvedFrame}
// so the Windows pipeline preserves the placeholder frame rather than dropping it.
//
// This guards the distinction between "cache returned nullopt" (SEH, keep the frame)
// and "cache returned InvalidFunctionId" (native, drop the frame).
TEST(FrameStoreNativeIpTest, GetFrame_WithCache_CachedPathNullopt_ReturnsResolvedPlaceholder)
{
    auto mockProfiler = MockProfilerInfo{};

    auto cache = std::make_unique<ManagedCodeCache>(&mockProfiler);
    cache->Initialize();

    // Register an R2R module range so GetFunctionId falls through to
    // GetFunctionFromIP_Original (which wraps the ICorProfilerInfo call in __try/__except).
    const uintptr_t r2rCodeStart = 0xC0000000;
    const uintptr_t r2rCodeEnd   = 0xC000FFFF;
    const uintptr_t ipInR2R      = r2rCodeStart + 0x500;

    std::vector<ModuleCodeRange> moduleRanges;
    moduleRanges.emplace_back(r2rCodeStart, r2rCodeEnd);
    cache->AddModuleRangesToCache(std::move(moduleRanges));

    // Simulate a crash during GetFunctionFromIP by raising an access violation.
    // Mirrors the real-world scenario where the CLR unloads the module containing
    // the target symbol while we resolve the IP.
    EXPECT_CALL(mockProfiler, GetFunctionFromIP(reinterpret_cast<LPCBYTE>(ipInR2R), _))
        .WillOnce([](LPCBYTE, FunctionID*) -> HRESULT {
            ::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return S_OK; // unreachable
        });

    FrameStore frameStore(
        /*pCorProfilerInfo*/ &mockProfiler,
        /*pConfiguration  */ nullptr,
        /*pDebugInfoStore */ nullptr,
        /*pManagedCodeCache*/ cache.get());

    // Sanity-check the upstream contract: the cache reports nullopt (SEH path).
    ASSERT_FALSE(cache->GetFunctionId(ipInR2R).has_value());

    // Re-arm the mock for the GetFrame call.
    EXPECT_CALL(mockProfiler, GetFunctionFromIP(reinterpret_cast<LPCBYTE>(ipInR2R), _))
        .WillOnce([](LPCBYTE, FunctionID*) -> HRESULT {
            ::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return S_OK;
        });

    auto [isResolved, frameInfo] = frameStore.GetFrame(ipInR2R);

    EXPECT_TRUE(isResolved) << "When the cache signals SEH (nullopt), FrameStore must "
                               "keep the frame (isResolved=true) to preserve the "
                               "legacy Windows placeholder behavior.";
    EXPECT_EQ(std::string(frameInfo.Frame), std::string(NotResolvedFrameText));

    cache.reset();
}
#endif
