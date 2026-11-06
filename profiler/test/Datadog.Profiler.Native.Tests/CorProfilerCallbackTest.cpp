// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "CorProfilerCallback.h"
#include "ProfilerMockedInterface.h"

TEST(CorProfilerCallbackTest, ManagedCodeCacheRequiresMainProfiler)
{
    auto configuration = std::make_shared<testing::NiceMock<MockConfiguration>>();
    ON_CALL(*configuration, UseManagedCodeCache()).WillByDefault(testing::Return(true));
    CorProfilerCallback profiler(configuration);

    BOOL notificationOnly = TRUE;
    ASSERT_EQ(S_OK, profiler.LoadAsNotificationOnly(&notificationOnly));
    EXPECT_EQ(FALSE, notificationOnly);
}

TEST(CorProfilerCallbackTest, NotificationLoadingWithoutManagedCodeCache)
{
    auto configuration = std::make_shared<testing::NiceMock<MockConfiguration>>();
    ON_CALL(*configuration, UseManagedCodeCache()).WillByDefault(testing::Return(false));
    CorProfilerCallback profiler(configuration);

    BOOL notificationOnly = FALSE;
    ASSERT_EQ(S_OK, profiler.LoadAsNotificationOnly(&notificationOnly));
#ifdef ARM64
    // ARM64 cannot run as a notification profiler even with the cache disabled.
    EXPECT_EQ(FALSE, notificationOnly);
#else
    EXPECT_EQ(TRUE, notificationOnly);
#endif
}
