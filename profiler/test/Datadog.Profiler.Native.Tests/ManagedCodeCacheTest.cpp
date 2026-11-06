// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "CounterMetric.h"
#include "ManagedCodeCache.h"
#include "MetricsRegistry.h"
#include "MockProfilerInfo.h"

#include <thread>
#include <chrono>

using namespace testing;

// Test fixture
class ManagedCodeCacheTest : public Test {
protected:
    MockProfilerInfo* mockProfiler;
    MetricsRegistry metricsRegistry;
    std::unique_ptr<ManagedCodeCache> cache;

    void SetUp() override {
        mockProfiler = new MockProfilerInfo();
        cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
        cache->Initialize();
    }

    void TearDown() override {
        cache.reset();
        mockProfiler->Release();
        mockProfiler = nullptr;
    }

    // Helper: Setup mock for AddFunction scenario
    void SetupMockCodeInfo(FunctionID funcId, uintptr_t start, ULONG32 size) {
        EXPECT_CALL(*mockProfiler, GetCodeInfo2(funcId, _, _, _))
            .WillOnce([start, size](FunctionID, ULONG32, ULONG32* pcCodeInfos,
                                   COR_PRF_CODE_INFO codeInfos[]) {
                if (pcCodeInfos != nullptr) {
                    *pcCodeInfos = 1;
                }
                if (codeInfos != nullptr) {
                    codeInfos[0].startAddress = start;
                    codeInfos[0].size = size;
                }
                return S_OK;
            });
    }

    // Helper: read (and reset) the IsManaged lock-failure counter.
    // GetMetrics() exchanges the underlying atomic to 0, so each call returns the
    // number of failures recorded since the previous read.
    uint64_t GetLockFailureCount() {
        auto metric = metricsRegistry.GetOrRegister<CounterMetric>("dotnet_managed_code_cache_lock_failures");
        auto metrics = metric->GetMetrics();
        return metrics.empty() ? 0 : static_cast<uint64_t>(metrics.front().second);
    }

    // Shortcut to get only the function id from the returned FunctionInfo
    FunctionID GetFuncIdOr0(uintptr_t ip) {
        auto info = cache->GetFunctionInfo(ip);
        return info.has_value() ? info->FunctionId : 0;
    }
};

// Test: Single code range
TEST_F(ManagedCodeCacheTest, AddFunction_SingleRange_GetFunctionInfoReturnsCorrect) {
    FunctionID testFuncId = 12345;
    uintptr_t codeStart = 0x1000;
    ULONG32 codeSize = 0x200;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);

    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Test IPs within range
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart));
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart + 0x100));
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart + codeSize - 1));

    // IPs outside range should return InvalidFunctionId (still "found", just not managed)
    auto beforeStartIp = cache->GetFunctionInfo(codeStart - 1);
    EXPECT_TRUE(beforeStartIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, beforeStartIp->FunctionId);
    auto borderIp = cache->GetFunctionInfo(codeStart + codeSize);
    EXPECT_TRUE(borderIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, borderIp->FunctionId);
}

TEST_F(ManagedCodeCacheTest, AddFunction_ReusedAddress_ResolvesLatestFunctionAcrossPages) {
    // The range crosses a 64KB page boundary, so both pages must prefer the new function.
    constexpr uintptr_t start = 0xff80;
    constexpr ULONG32 size = 0x100;
    SetupMockCodeInfo(123, start, size);
    cache->AddFunction(123, false, /*reJitId*/ 0);
    SetupMockCodeInfo(456, start, size);
    cache->AddFunction(456, false, /*reJitId*/ 0);

    EXPECT_EQ(GetFuncIdOr0(start), 456);
    EXPECT_EQ(GetFuncIdOr0(0x10000), 456);
    EXPECT_EQ(GetFuncIdOr0(start + size - 1), 456);
    EXPECT_FALSE(cache->IsManaged(start + size).value());
}

TEST_F(ManagedCodeCacheTest, AddFunction_ReusedFunctionIdAndAddress_ResolvesLatestRegistration) {
    // A FunctionID can be reused too. An older matching entry must not suppress
    // a new registration; repeating the latest registration must also be safe.
    for (FunctionID functionId : {123, 456, 123, 123}) {
        SetupMockCodeInfo(functionId, 0x1000, 0x100);
        cache->AddFunction(functionId, false, /*reJitId*/ 0);
        EXPECT_EQ(GetFuncIdOr0(0x1000), functionId);
        EXPECT_EQ(GetFuncIdOr0(0x10ff), functionId);
    }
}

TEST_F(ManagedCodeCacheTest, AddFunction_ReusedAddress_LargerRangeIncludesNewTail) {
    SetupMockCodeInfo(123, 0x1000, 0x100);
    cache->AddFunction(123, false, /*reJitId*/ 0);
    SetupMockCodeInfo(456, 0x1000, 0x200);
    cache->AddFunction(456, false, /*reJitId*/ 0);

    EXPECT_EQ(GetFuncIdOr0(0x1050), 456);
    EXPECT_EQ(GetFuncIdOr0(0x1150), 456);
    EXPECT_EQ(GetFuncIdOr0(0x11ff), 456);
    EXPECT_FALSE(cache->IsManaged(0x1200).value());
}

// GetCodeInfo2 reports the default native body even after a tier promotion.
TEST_F(ManagedCodeCacheTest, AddFunction_LegacyGetCodeInfo2_CannotDiscoverPromotedBody) {
    FunctionID testFuncId = 67890;
    uintptr_t tier0Start = 0x2000;
    ULONG32 tier0Size = 0x100;
    uintptr_t tier1Start = 0x3000;

    // The first callback discovers the default Tier 0 body.
    SetupMockCodeInfo(testFuncId, tier0Start, tier0Size);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    EXPECT_EQ(testFuncId, GetFuncIdOr0(tier0Start + 0x50));

    // A later callback still gets Tier 0 from GetCodeInfo2, not the promoted Tier 1 body.
    SetupMockCodeInfo(testFuncId, tier0Start, tier0Size);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    EXPECT_EQ(testFuncId, GetFuncIdOr0(tier0Start + 0x50));
    EXPECT_FALSE(cache->IsManaged(tier1Start + 0x100).value());
}

// GetCodeInfo2 keeps returning Tier 0 even from later JIT callbacks. Info9
// enumerates all native versions, including optimized and OSR code bodies.
TEST_F(ManagedCodeCacheTest, AddFunction_EnumeratesNativeVersionsAfterTiering) {
    cache.reset();
    mockProfiler->supportsInfo9 = true;
    cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
    ASSERT_TRUE(cache->Initialize());

    const FunctionID functionId = 123;
    std::vector<UINT_PTR> versions{0x1000};
    EXPECT_CALL(*mockProfiler, GetCodeInfo2(_, _, _, _)).Times(0);
    EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(functionId, 0, _, _, _))
        .WillRepeatedly([&](FunctionID, ReJITID, ULONG32 capacity, ULONG32* count, UINT_PTR* starts) {
            *count = versions.size();
            for (size_t i = 0; i < std::min<size_t>(capacity, versions.size()); ++i) starts[i] = versions[i];
            return S_OK;
        });
    EXPECT_CALL(*mockProfiler, GetCodeInfo4(_, _, _, _))
        .WillRepeatedly([](UINT_PTR start, ULONG32, ULONG32* count, COR_PRF_CODE_INFO* infos) {
            *count = 1;
            infos[0] = {start, 0x100};
            return S_OK;
        });

    cache->AddFunction(functionId, false, /*reJitId*/ 0);
    EXPECT_EQ(GetFuncIdOr0(0x1050), functionId);
    EXPECT_FALSE(cache->IsManaged(0x2050).value());

    versions.push_back(0x2000);
    versions.push_back(0x3000);
    cache->AddFunction(functionId, false, /*reJitId*/ 0);
    for (auto start : versions) {
        EXPECT_TRUE(cache->IsManaged(start + 0x50).value());
        EXPECT_EQ(GetFuncIdOr0(start + 0x50), functionId);
    }
    EXPECT_FALSE(cache->IsManaged(0x3100).value());
}

TEST_F(ManagedCodeCacheTest, AddFunction_EnumeratesRequestedRejitVersion) {
    cache.reset();
    mockProfiler->supportsInfo9 = true;
    cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
    ASSERT_TRUE(cache->Initialize());

    EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 7, _, _, _))
        .WillRepeatedly([](FunctionID, ReJITID, ULONG32 capacity, ULONG32* count, UINT_PTR* starts) {
            *count = 1;
            if (capacity != 0) starts[0] = 0x5000;
            return S_OK;
        });
    EXPECT_CALL(*mockProfiler, GetCodeInfo4(0x5000, _, _, _))
        .WillOnce([](UINT_PTR start, ULONG32, ULONG32* count, COR_PRF_CODE_INFO* infos) {
            *count = 1;
            infos[0] = {start, 0x100};
            return S_OK;
        });
    cache->AddFunction(123, false, 7);
    EXPECT_EQ(GetFuncIdOr0(0x5050), 123);
}

TEST_F(ManagedCodeCacheTest, AddFunction_RetriesWhenNativeVersionsGrowDuringEnumeration) {
    cache.reset();
    mockProfiler->supportsInfo9 = true;
    cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
    ASSERT_TRUE(cache->Initialize());

    EXPECT_CALL(*mockProfiler, GetCodeInfo2(_, _, _, _)).Times(0);
    {
        InSequence sequence;
        EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 7, 0, _, nullptr))
            .WillOnce(DoAll(SetArgPointee<3>(1), Return(S_OK)));
        // The runtime reports the new count without filling an undersized buffer.
        EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 7, 1, _, NotNull()))
            .WillOnce(DoAll(SetArgPointee<3>(2), Return(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))));
        EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 7, 2, _, NotNull()))
            .WillOnce(DoAll(SetArgPointee<3>(3), Return(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))));
        EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 7, 3, _, NotNull()))
            .WillOnce([](FunctionID, ReJITID, ULONG32, ULONG32* count, UINT_PTR* starts) {
                *count = 3;
                starts[0] = 0x1000;
                starts[1] = 0x2000;
                starts[2] = 0x3000;
                return S_OK;
            });
    }
    for (UINT_PTR start : {0x1000, 0x2000, 0x3000}) {
        EXPECT_CALL(*mockProfiler, GetCodeInfo4(start, _, _, _))
            .WillOnce([](UINT_PTR start, ULONG32, ULONG32* count, COR_PRF_CODE_INFO* infos) {
                *count = 1;
                infos[0] = {start, 0x100};
                return S_OK;
            });
    }

    cache->AddFunction(123, false, 7);
    for (UINT_PTR start : {0x1000, 0x2000, 0x3000}) {
        EXPECT_TRUE(cache->IsManaged(start + 0x50).value());
        EXPECT_EQ(GetFuncIdOr0(start + 0x50), 123);
    }
}

TEST_F(ManagedCodeCacheTest, AddFunction_BoundsRetriesWhenNativeVersionsKeepGrowing) {
    cache.reset();
    mockProfiler->supportsInfo9 = true;
    cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
    ASSERT_TRUE(cache->Initialize());

    EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 0, 0, _, nullptr))
        .WillOnce(DoAll(SetArgPointee<3>(1), Return(S_OK)));
    EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 0, Gt(0), _, NotNull()))
        .Times(3)
        .WillRepeatedly([](FunctionID, ReJITID, ULONG32 capacity, ULONG32* count, UINT_PTR*) {
            *count = capacity + 1;
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        });
    EXPECT_CALL(*mockProfiler, GetCodeInfo4(_, _, _, _)).Times(0);
    SetupMockCodeInfo(123, 0x1000, 0x100);

    cache->AddFunction(123, false, /*reJitId*/ 0);
    EXPECT_EQ(GetFuncIdOr0(0x1050), 123);
    EXPECT_FALSE(cache->IsManaged(0x2050).value());
}

TEST_F(ManagedCodeCacheTest, AddFunction_FallsBackWhenNativeVersionEnumerationFails) {
    cache.reset();
    mockProfiler->supportsInfo9 = true;
    cache = std::make_unique<ManagedCodeCache>(mockProfiler, metricsRegistry);
    ASSERT_TRUE(cache->Initialize());
    EXPECT_CALL(*mockProfiler, GetNativeCodeStartAddresses(123, 0, _, _, _))
        .WillOnce(Return(E_FAIL));
    SetupMockCodeInfo(123, 0x6000, 0x100);
    cache->AddFunction(123, true, /*reJitId*/ 0);
    auto info = cache->GetFunctionInfo(0x6050);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->FunctionId, 123);
    EXPECT_TRUE(info->IsDynamic);
}

// Test: IsManaged for valid managed IP
TEST_F(ManagedCodeCacheTest, IsManaged_ValidManagedIP_ReturnsTrue) {
    FunctionID testFuncId = 11111;
    uintptr_t codeStart = 0x4000;
    ULONG32 codeSize = 0x150;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    EXPECT_TRUE(cache->IsManaged(codeStart + 0x50));
}

// Test: IsManaged for invalid IP
TEST_F(ManagedCodeCacheTest, IsManaged_InvalidIP_ReturnsFalse) {
    auto deadbeef = cache->IsManaged(0xDEADBEEF);
    EXPECT_TRUE(deadbeef.has_value());
    EXPECT_FALSE(deadbeef.value());
    auto zero = cache->IsManaged(0);
    EXPECT_TRUE(zero.has_value());
    EXPECT_FALSE(zero.value());
}

// Test: Multiple functions don't interfere
TEST_F(ManagedCodeCacheTest, AddFunction_MultipleFunctions_NoInterference) {
    FunctionID func1 = 100;
    FunctionID func2 = 200;
    uintptr_t code1Start = 0x5000;
    uintptr_t code2Start = 0x6000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(func1, code1Start, codeSize);
    SetupMockCodeInfo(func2, code2Start, codeSize);

    cache->AddFunction(func1, /*isDynamic*/ false, /*reJitId*/ 0);
    cache->AddFunction(func2, /*isDynamic*/ false, /*reJitId*/ 0);
    EXPECT_EQ(func1, GetFuncIdOr0(code1Start + 0x50));
    EXPECT_EQ(func2, GetFuncIdOr0(code2Start + 0x50));

    // No cross-contamination
    auto outside = cache->GetFunctionInfo(code1Start + codeSize + 10);
    EXPECT_TRUE(outside.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, outside->FunctionId);
}

// Test: Thread safety (concurrent AddFunction calls)
TEST_F(ManagedCodeCacheTest, AddFunction_ConcurrentCalls_ThreadSafe) {
    const int numThreads = 4;
    const int functionsPerThread = 10;

    // Pre-setup all mock expectations (GoogleMock is not thread-safe for EXPECT_CALL)
    for (int t = 0; t < numThreads; t++) {
        for (int i = 0; i < functionsPerThread; i++) {
            FunctionID funcId = (t * 1000) + i;
            uintptr_t codeStart = 0x10000 + (funcId * 0x1000);
            ULONG32 codeSize = 0x100;

            SetupMockCodeInfo(funcId, codeStart, codeSize);
        }
    }

    // Now spawn threads to call AddFunction concurrently
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([this, t, functionsPerThread]() {
            for (int i = 0; i < functionsPerThread; i++) {
                FunctionID funcId = (t * 1000) + i;
                cache->AddFunction(funcId, /*isDynamic*/ false, /*reJitId*/ 0);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify every function is retrievable by IP
    for (int t = 0; t < numThreads; t++) {
        for (int i = 0; i < functionsPerThread; i++) {
            FunctionID funcId = (t * 1000) + i;
            uintptr_t codeStart = 0x10000 + (funcId * 0x1000);

            auto result = cache->GetFunctionInfo(codeStart + 0x50);
            EXPECT_TRUE(result.has_value())
                << "Function " << funcId << " not found at IP 0x" << std::hex << (codeStart + 0x50);
            EXPECT_EQ(funcId, result.has_value() ? result->FunctionId : 0)
                << "Wrong FunctionID for function " << funcId;

            EXPECT_TRUE(cache->IsManaged(codeStart + 0x50))
                << "IsManaged returned false for function " << funcId;
        }
    }

    // Verify an IP outside all registered ranges returns InvalidFunctionId
    auto outside = cache->GetFunctionInfo(0xDEAD);
    EXPECT_TRUE(outside.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, outside->FunctionId);
    auto outsideIsManaged = cache->IsManaged(0xDEAD);
    EXPECT_TRUE(outsideIsManaged.has_value());
    EXPECT_FALSE(outsideIsManaged.value());
}

// Test: IsManaged (no blocking)
TEST_F(ManagedCodeCacheTest, IsManaged_ConcurrentAccess) {
    FunctionID testFuncId = 999;
    uintptr_t codeStart = 0x7000;
    ULONG32 codeSize = 0x200;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Concurrent IsManaged calls (simulating signal handler scenario)
    const int numCalls = 1000;
    std::atomic<int> successCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([this, codeStart, &successCount, numCalls]() {
            for (int i = 0; i < numCalls; i++) {
                if (cache->IsManaged(codeStart + 0x50)) {
                    successCount++;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All calls should succeed (no deadlocks)
    EXPECT_EQ(numCalls * 4, successCount.load());
}

// Test: Boundary conditions
TEST_F(ManagedCodeCacheTest, GetFunctionInfo_BoundaryIPs_CorrectBehavior) {
    FunctionID testFuncId = 555;
    uintptr_t codeStart = 0x8000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Exact boundaries
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart));  // First byte (inclusive)
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart + codeSize - 1));  // Last byte (inclusive)

    // Just outside boundaries
    auto beforeStartIp = cache->GetFunctionInfo(codeStart - 1);
    EXPECT_TRUE(beforeStartIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, beforeStartIp->FunctionId);
    auto borderIp = cache->GetFunctionInfo(codeStart + codeSize);
    EXPECT_TRUE(borderIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, borderIp->FunctionId);
}

// Regression: GetCodeRanges used `startAddress + size - 1` without guarding
// size==0.  For startAddress==0 this gives endAddress==UINTPTR_MAX and the
// page-insertion loop `for (page=0; page<=UINTPTR_MAX/pageSize; ++page)` runs
// for billions of iterations, permanently hanging the (now synchronous) insert.
// For non-zero startAddress the loop silently skips (endPage < startPage) but
// the degenerate CodeRange still pollutes the sorted range vector.
TEST_F(ManagedCodeCacheTest, AddFunction_ZeroSizeRange_DoesNotPolluteCacheNonZeroBase) {
    FunctionID testFuncId = 777;
    uintptr_t codeStart = 0x9000;
    ULONG32 codeSize = 0;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // A zero-size range has no valid code; no IP should be reported as managed.
    auto codeStartIp = cache->IsManaged(codeStart);
    EXPECT_TRUE(codeStartIp.has_value());
    EXPECT_FALSE(codeStartIp.value())
        << "IP at startAddress of a size-0 range must not be managed";
    auto codeStartMinusOne = cache->IsManaged(codeStart - 1);
    EXPECT_TRUE(codeStartMinusOne.has_value());
    EXPECT_FALSE(codeStartMinusOne.value())
        << "IP just before startAddress must not be managed";
    auto codeStartPlusOneThousand = cache->IsManaged(codeStart + 0x1000);
    EXPECT_TRUE(codeStartPlusOneThousand.has_value());
    EXPECT_FALSE(codeStartPlusOneThousand.value())
        << "Unrelated IP above a size-0 range must not be managed";
}

// For startAddress==0 with size==0 the page loop would run from page 0 to
// ~UINTPTR_MAX/pageSize, hanging forever.  After the fix AddFunction (now
// synchronous) must return promptly and IsManaged must return false.
TEST_F(ManagedCodeCacheTest, AddFunction_ZeroSizeRangeAtAddressZero_DoesNotHang) {
    FunctionID testFuncId = 778;
    uintptr_t codeStart = 0;
    ULONG32 codeSize = 0;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    // If the synchronous insert is stuck this call blocks until the test times out.
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);

    auto codeStartPlusOneThousand = cache->IsManaged(0x1000);
    EXPECT_TRUE(codeStartPlusOneThousand.has_value());
    EXPECT_FALSE(codeStartPlusOneThousand.value())
        << "Unrelated IP above a size-0 range must not be managed";
}

// Test: Very large code range
TEST_F(ManagedCodeCacheTest, AddFunction_LargeCodeRange_WorksCorrectly) {
    FunctionID testFuncId = 888;
    uintptr_t codeStart = 0x100000;
    ULONG32 codeSize = 0x10000;  // 64KB

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Test various points in large range
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart));
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart + 0x8000));
    EXPECT_EQ(testFuncId, GetFuncIdOr0(codeStart + codeSize - 1));  // End
}

// Test: Null IP
TEST_F(ManagedCodeCacheTest, GetFunctionInfo_NullIP_ReturnsInvalidFunctionId) {
    auto nullIp = cache->GetFunctionInfo(0);
    EXPECT_TRUE(nullIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, nullIp->FunctionId);
}

// Test: GetCodeInfo2 failure handling
TEST_F(ManagedCodeCacheTest, AddFunction_GetCodeInfo2Fails_HandledGracefully) {
    FunctionID testFuncId = 999;

    EXPECT_CALL(*mockProfiler, GetCodeInfo2(testFuncId, _, _, _))
        .WillOnce(Return(E_FAIL));

    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Should not crash
    auto nullIp = cache->GetFunctionInfo(0x1000);
    EXPECT_TRUE(nullIp.has_value());
    EXPECT_EQ(ManagedCodeCache::InvalidFunctionId, nullIp->FunctionId);
}

// Test: isDynamic given to AddFunction is returned by GetFunctionInfo
TEST_F(ManagedCodeCacheTest, AddFunction_DynamicMethod_IsDynamicPropagatedToLookup) {
    FunctionID testFuncId = 424242;
    uintptr_t codeStart = 0xE000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ true, /*reJitId*/ 0);

    auto info = cache->GetFunctionInfo(codeStart + 0x50);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(testFuncId, info->FunctionId);
    EXPECT_TRUE(info->IsDynamic)
        << "isDynamic passed to AddFunction must be carried through to GetFunctionInfo";
}

// Test: the regular (non-dynamic) registration path reports IsDynamic == false.
TEST_F(ManagedCodeCacheTest, AddFunction_RegularMethod_IsDynamicFalse) {
    FunctionID testFuncId = 434343;
    uintptr_t codeStart = 0xF000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);

    auto info = cache->GetFunctionInfo(codeStart + 0x50);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(testFuncId, info->FunctionId);
    EXPECT_FALSE(info->IsDynamic);
}

// Test: the R2R fallback path (GetFunctionFromIP_Original) always returns IsDynamic = false
// because precompiled code is never dynamic.
TEST_F(ManagedCodeCacheTest, GetFunctionInfo_R2RFallbackSucceeds_IsDynamicFalse) {
    uintptr_t r2rCodeStart = 0xB1000000;
    uintptr_t r2rCodeEnd   = 0xB100FFFF;
    uintptr_t ipInR2R      = r2rCodeStart + 0x500;
    FunctionID r2rFuncId = 0xABCD;

    std::vector<ModuleCodeRange> moduleRanges;
    moduleRanges.emplace_back(r2rCodeStart, r2rCodeEnd);
    cache->AddModuleRangesToCache(std::move(moduleRanges));

    EXPECT_CALL(*mockProfiler, GetFunctionFromIP(reinterpret_cast<LPCBYTE>(ipInR2R), _))
        .WillOnce([r2rFuncId](LPCBYTE, FunctionID* pFunctionId) -> HRESULT {
            *pFunctionId = r2rFuncId;
            return S_OK;
        });
    // On success, the range is synchronously added to the cache: this calls GetCodeInfo2
    SetupMockCodeInfo(r2rFuncId, r2rCodeStart, static_cast<ULONG32>(r2rCodeEnd - r2rCodeStart + 1));

    auto info = cache->GetFunctionInfo(ipInR2R);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(r2rFuncId, info->FunctionId);
    EXPECT_FALSE(info->IsDynamic);
}

#ifdef _WINDOWS
// Test: On Windows, GetFunctionFromIP can crash (e.g. module unloaded concurrently).
// The SEH __try/__except in GetFunctionFromIP_Original must catch the access violation
// and GetFunctionInfo must return std::nullopt to signal the failure to the caller.
TEST_F(ManagedCodeCacheTest, GetFunctionInfo_GetFunctionFromIPRaisesAccessViolation_ReturnsNullopt) {
    // Register an R2R module range so that GetFunctionInfo falls through to
    // GetFunctionFromIP_Original (which wraps the ICorProfilerInfo call in __try/__except).
    uintptr_t r2rCodeStart = 0xB0000000;
    uintptr_t r2rCodeEnd   = 0xB000FFFF;
    uintptr_t ipInR2R      = r2rCodeStart + 0x500;

    std::vector<ModuleCodeRange> moduleRanges;
    moduleRanges.emplace_back(r2rCodeStart, r2rCodeEnd);
    cache->AddModuleRangesToCache(std::move(moduleRanges));

    // Simulate a crash during GetFunctionFromIP by raising an access violation.
    // This mirrors the real-world scenario where the CLR unloads the module containing
    // the target symbol while we are resolving the IP.
    EXPECT_CALL(*mockProfiler, GetFunctionFromIP(reinterpret_cast<LPCBYTE>(ipInR2R), _))
        .WillOnce([](LPCBYTE, FunctionID*) -> HRESULT {
            ::RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            return S_OK; // unreachable
        });

    auto result = cache->GetFunctionInfo(ipInR2R);
    EXPECT_FALSE(result.has_value())
        << "GetFunctionInfo should return std::nullopt when GetFunctionFromIP raises an access violation";
}
#endif

// Test: IsManaged is signal-safe - it must return std::nullopt rather than block
// when another thread holds the writer lock on the pages mutex. This guards the
// contract relied on by the HybridUnwinder in a signal handler on ARM64.
TEST_F(ManagedCodeCacheTest, IsManaged_WriterHoldsPagesMutex_ReturnsNullopt) {
    FunctionID testFuncId = 321;
    uintptr_t codeStart = 0xC000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);
    // Sanity: with no contention, IsManaged returns a concrete value.
    auto baseline = cache->IsManaged(codeStart + 0x50);
    ASSERT_TRUE(baseline.has_value());
    EXPECT_TRUE(baseline.value());

    // Simulate signal-handler contention: another thread holds the pages mutex
    // exclusively. IsManaged must fail the try_to_lock and return std::nullopt
    // instead of blocking (which would deadlock if called from a signal handler).
    std::atomic<bool> writerHoldsLock{false};
    std::atomic<bool> readerDone{false};
    std::thread writer([&]() {
        auto lock = cache->LockPagesMutexExclusiveForTest();
        writerHoldsLock.store(true);
        // Hold the lock until the reader side of the test has observed the nullopt.
        while (!readerDone.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Wait for the writer to actually hold the lock before probing.
    while (!writerHoldsLock.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto contended = cache->IsManaged(codeStart + 0x50);
    EXPECT_FALSE(contended.has_value())
        << "IsManaged must return nullopt under writer-lock contention so the "
           "signal handler can back off instead of deadlocking.";

    readerDone.store(true);
    writer.join();

    // Sanity: once contention clears, IsManaged resumes normal behavior.
    auto afterRelease = cache->IsManaged(codeStart + 0x50);
    ASSERT_TRUE(afterRelease.has_value());
    EXPECT_TRUE(afterRelease.value());
}

// Test: IsManaged increments the lock-failure metric when it cannot acquire the
// pages mutex within the timeout, and leaves it untouched on the success path.
TEST_F(ManagedCodeCacheTest, IsManaged_LockAcquisitionFailure_IncrementsMetric) {
    FunctionID testFuncId = 0x4321;
    uintptr_t codeStart = 0xD000;
    ULONG32 codeSize = 0x100;

    SetupMockCodeInfo(testFuncId, codeStart, codeSize);
    cache->AddFunction(testFuncId, /*isDynamic*/ false, /*reJitId*/ 0);

    // Success path must not increment the failure metric.
    ASSERT_TRUE(cache->IsManaged(codeStart + 0x50).value_or(false));
    EXPECT_EQ(0u, GetLockFailureCount());

    // Hold the pages mutex exclusively so IsManaged times out and returns nullopt.
    std::atomic<bool> writerHoldsLock{false};
    std::atomic<bool> readerDone{false};
    std::thread writer([&]() {
        auto lock = cache->LockPagesMutexExclusiveForTest();
        writerHoldsLock.store(true);
        while (!readerDone.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!writerHoldsLock.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto contended = cache->IsManaged(codeStart + 0x50);
    EXPECT_FALSE(contended.has_value());

    readerDone.store(true);
    writer.join();

    // The failed acquisition must have been recorded exactly once.
    EXPECT_EQ(1u, GetLockFailureCount());
}

// Test: IsManaged must also return nullopt when the writer holds the modules
// mutex exclusively and the probed IP is not in any JIT page (i.e. the code path
// falls through to IsCodeInR2RModule(ip, /*signalSafe*/true)).
TEST_F(ManagedCodeCacheTest, IsManaged_WriterHoldsModulesMutex_ReturnsNullopt) {
    // Probe an IP that is not covered by any registered JIT range, so IsManaged
    // falls through to IsCodeInR2RModule which try_to_locks _modulesMutex.
    const uintptr_t ipOutsideJit = 0xDEADBEEF;

    std::atomic<bool> writerHoldsLock{false};
    std::atomic<bool> readerDone{false};
    std::thread writer([&]() {
        auto lock = cache->LockModulesMutexExclusiveForTest();
        writerHoldsLock.store(true);
        while (!readerDone.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!writerHoldsLock.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto contended = cache->IsManaged(ipOutsideJit);
    EXPECT_FALSE(contended.has_value())
        << "IsManaged must return nullopt when the modules-mutex writer is active "
           "and the IP is not in any JIT page (R2R fallback path).";

    readerDone.store(true);
    writer.join();
}

// Test: IsManaged falls back to R2R module check when IP is not in the JIT page map
TEST_F(ManagedCodeCacheTest, IsManaged_IPInR2RModule_NotInPageMap_ReturnsTrue) {
    // Register an R2R module range directly (bypassing PE parsing)
    // Use an address that has no JIT-compiled code registered on its page
    uintptr_t r2rCodeStart = 0xA0000000;
    uintptr_t r2rCodeEnd   = 0xA000FFFF;

    std::vector<ModuleCodeRange> moduleRanges;
    moduleRanges.emplace_back(r2rCodeStart, r2rCodeEnd);

    cache->AddModuleRangesToCache(std::move(moduleRanges));

    // An IP within the R2R range but with no JIT page entry should still be detected as managed
    uintptr_t ipInR2R = r2rCodeStart + 0x500;
    EXPECT_TRUE(cache->IsManaged(ipInR2R))
        << "IsManaged should return true for an IP in an R2R module even when the JIT page map has no entry for that page";

    // An IP outside both the JIT page map and any R2R module should still be false
    auto outsideIsManaged = cache->IsManaged(0xDEADBEEF);
    EXPECT_TRUE(outsideIsManaged.has_value());
    EXPECT_FALSE(outsideIsManaged.value());
}
