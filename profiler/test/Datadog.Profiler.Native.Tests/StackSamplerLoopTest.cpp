#ifdef LINUX

#include "gtest/gtest.h"

#include "ManagedThreadList.h"
#include "OsSpecificApi.h"
#include "OpSysTools.h"
#include "StackSamplerLoop.h"
#include "MetricsRegistry.h"
#include "CpuProfilerType.h"
#include "ProfilerMockedInterface.h"
#include "ThreadsCpuManagerHelper.h"
#include "StubCorProfilerInfo4.h"

using ::testing::Return;

class StackSamplerLoopTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto [config, mock] = CreateConfiguration();
        _configuration = std::move(config);
        _pMockConfiguration = &mock;

        EXPECT_CALL(*_pMockConfiguration, WalltimeThreadsThreshold()).WillRepeatedly(Return(5));
        EXPECT_CALL(*_pMockConfiguration, CpuThreadsThreshold()).WillRepeatedly(Return(64));
        EXPECT_CALL(*_pMockConfiguration, CodeHotspotsThreadsThreshold()).WillRepeatedly(Return(10));
        EXPECT_CALL(*_pMockConfiguration, IsWallTimeProfilingEnabled()).WillRepeatedly(Return(false));
        EXPECT_CALL(*_pMockConfiguration, IsCpuProfilingEnabled()).WillRepeatedly(Return(true));
        EXPECT_CALL(*_pMockConfiguration, GetCpuProfilerType()).WillRepeatedly(Return(CpuProfilerType::ManualCpuTime));
        EXPECT_CALL(*_pMockConfiguration, IsInternalMetricsEnabled()).WillRepeatedly(Return(false));
        EXPECT_CALL(*_pMockConfiguration, CpuWallTimeSamplingRate()).WillRepeatedly(Return(std::chrono::nanoseconds(9000000)));
    }

    std::unique_ptr<StackSamplerLoop> CreateLoop()
    {
        return std::make_unique<StackSamplerLoop>(
            &_stubProfilerInfo,
            _configuration.get(),
            nullptr, // StackFramesCollectorBase
            nullptr, // StackSamplerLoopManager
            &_cpuManager,
            &_threadList,
            &_codeHotspotThreadList,
            nullptr, // ICollector<RawWallTimeSample>
            nullptr, // ICollector<RawCpuSample>
            _metricsRegistry);
    }

    static void CallResetThreadsCpuConsumption(StackSamplerLoop& loop)
    {
        loop.ResetThreadsCpuConsumption();
    }

    static bool IsTargetThreadNull(const StackSamplerLoop& loop)
    {
        return loop._targetThread == nullptr;
    }

    StubCorProfilerInfo4 _stubProfilerInfo;
    std::unique_ptr<IConfiguration> _configuration;
    MockConfiguration* _pMockConfiguration = nullptr;
    ThreadsCpuManagerHelper _cpuManager;
    ManagedThreadList _threadList{nullptr};
    ManagedThreadList _codeHotspotThreadList{nullptr};
    MetricsRegistry _metricsRegistry;
};

// Regression test for https://github.com/grafana/pyroscope-dotnet/issues/259
//
// When StackSamplerLoop is freshly created, _targetThread is null.
// ResetThreadsCpuConsumption() is called from MainLoop before any sampling
// iteration, so _targetThread has not been set yet.
//
// The bug: the loop called OsSpecificApi::IsRunning(_targetThread.get())
// passing nullptr, causing SIGSEGV.
//
// The fix: use the iterator variable it.get() instead.
TEST_F(StackSamplerLoopTest, IsRunning_Nullptr_Crashes)
{
    ASSERT_DEATH(
        OsSpecificApi::IsRunning(nullptr),
        ""
    );
}

TEST_F(StackSamplerLoopTest, ResetThreadsCpuConsumption_NullTargetThread_DoesNotCrash)
{
    auto currentTid = OpSysTools::GetThreadId();
    _threadList.GetOrCreate(1);
    _threadList.SetThreadOsInfo(1, currentTid, (HANDLE)(uintptr_t)currentTid);
    _threadList.GetOrCreate(2);
    _threadList.SetThreadOsInfo(2, currentTid, (HANDLE)(uintptr_t)currentTid);

    auto loop = CreateLoop();
    ASSERT_TRUE(IsTargetThreadNull(*loop));

    CallResetThreadsCpuConsumption(*loop);
}

#endif
