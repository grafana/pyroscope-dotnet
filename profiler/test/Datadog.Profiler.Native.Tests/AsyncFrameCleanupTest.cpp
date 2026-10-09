// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2025 Datadog, Inc.

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "AppDomainStoreHelper.h"
#include "AsyncScopeStore.h"
#include "CallstackProvider.h"
#include "FrameStoreHelper.h"
#include "MemoryResourceManager.h"
#include "ProfilerMockedInterface.h"
#include "RawSampleTransformer.h"
#include "RawWallTimeSample.h"
#include "RuntimeIdStoreHelper.h"
#include "SampleValueTypeProvider.h"

using namespace std::chrono_literals;

namespace {

// Real frame names from a profiled ASP.NET Core service. Stacks are written leaf-first,
// the order the sampler collects them in.
const std::string Box =
    "System.Runtime.CompilerServices!AsyncTaskMethodBuilder.AsyncStateMachineBox<"
    "System.Threading.Tasks.VoidTaskResult, Microsoft.AspNetCore.Session!SessionMiddleware.<Invoke>d__8>.MoveNext";
const std::string BuilderStart =
    "System.Runtime.CompilerServices!AsyncMethodBuilderCore.Start<"
    "Microsoft.AspNetCore.Diagnostics!DeveloperExceptionPageMiddlewareImpl.<Invoke>d__14>";
const std::string Dispatch = "System.Threading!ThreadPoolWorkQueue.Dispatch";
const std::string RunFromPool = "System.Threading!ExecutionContext.RunFromThreadPoolDispatchLoop";
const std::string WorkerThread = "System.Threading!PortableThreadPool.WorkerThread.WorkerThreadStart";
const std::string ThreadStart = "System.Threading!Thread.StartCallback";

const std::string CompressionBody =
    "Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.<InvokeCore>d__4.MoveNext";
const std::string CompressionKickoff =
    "Microsoft.AspNetCore.ResponseCompression!ResponseCompressionMiddleware.InvokeCore";
const std::string SessionInvoke = "Microsoft.AspNetCore.Session!SessionMiddleware.Invoke";
const std::string HostFiltering = "Microsoft.AspNetCore.HostFiltering!HostFilteringMiddleware.Invoke";

class AsyncFrameCleanupTest : public ::testing::Test
{
protected:
    AsyncFrameCleanupTest() :
        _callstackProvider{MemoryResourceManager::GetDefault()},
        _appDomainStore{1}
    {
        EXPECT_CALL(_runtimeIdStore, GetId(::testing::_)).WillRepeatedly(::testing::Return("rid"));
        Sample::ValuesCount = 1;
    }

    // Runs a leaf-first stack of frame names through the transformer and returns the
    // frame names the sample ends up with.
    std::vector<std::string> Clean(std::vector<std::string> const& frames,
                                   AsyncScopeStore* scopeStore = nullptr,
                                   std::uint32_t asyncScopeId = AsyncScopeStore::NoScope)
    {
        FrameStoreHelper frameStore{frames};
        RawSampleTransformer transformer(&frameStore, &_appDomainStore, &_runtimeIdStore, scopeStore);

        RawWallTimeSample raw;
        raw.Timestamp = 1ns;
        raw.Duration = 10ns;
        raw.AppDomainId = static_cast<AppDomainID>(1);
        raw.AsyncScopeId = asyncScopeId;
        raw.Stack = _callstackProvider.Get();
        for (std::size_t i = 0; i < frames.size(); i++)
        {
            raw.Stack.Add(i + 1);
        }

        std::vector<SampleValueTypeProvider::Offset> offsets{0};
        auto sample = transformer.Transform(raw, offsets);

        std::vector<std::string> names;
        for (auto const& frame : sample->GetCallstack())
        {
            names.emplace_back(frame.Frame);
        }
        return names;
    }

    CallstackProvider _callstackProvider;
    AppDomainStoreHelper _appDomainStore;
    MockRuntimeIdStore _runtimeIdStore;
};

} // namespace

TEST_F(AsyncFrameCleanupTest, RuntimePlumbingIsDroppedFromTheStack)
{
    // The worker thread root survives: it is not on the delivery path, it is where the
    // pool thread's own time belongs, and it is present on every pool stack, so keeping
    // it costs nothing in merging.
    EXPECT_THAT(Clean({CompressionBody, SessionInvoke, RunFromPool, Box, Dispatch, WorkerThread, ThreadStart}),
                ::testing::ElementsAre(CompressionKickoff, SessionInvoke, WorkerThread, ThreadStart));
}

TEST_F(AsyncFrameCleanupTest, TheKickoffFrameMergesIntoItsStateMachineBody)
{
    // The compiler-generated kickoff and the state machine body are the same method, so
    // they must not stack as two near-identical bars.
    EXPECT_THAT(Clean({CompressionBody, CompressionKickoff, SessionInvoke}),
                ::testing::ElementsAre(CompressionKickoff, SessionInvoke));
}

TEST_F(AsyncFrameCleanupTest, AKickoffFrameThatIsNotItsNeighboursOwnIsKept)
{
    // Only the pair belonging to the same method collapses; an unrelated method that
    // happens to sit next to a state machine body keeps its frame.
    EXPECT_THAT(Clean({CompressionBody, SessionInvoke, HostFiltering}),
                ::testing::ElementsAre(CompressionKickoff, SessionInvoke, HostFiltering));
}

TEST_F(AsyncFrameCleanupTest, TwoSamplesDifferingOnlyByPlumbingProduceTheSameStack)
{
    // This is the reported symptom: the same logical path lands in two flamegraph nodes
    // because a builder frame survived inlining in one sample and not the other.
    auto const withoutBuilderFrame = Clean({CompressionBody, SessionInvoke, ThreadStart});
    auto const withBuilderFrame = Clean({CompressionBody, BuilderStart, SessionInvoke, ThreadStart});

    EXPECT_EQ(withoutBuilderFrame, withBuilderFrame);
    EXPECT_THAT(withBuilderFrame, ::testing::ElementsAre(CompressionKickoff, SessionInvoke, ThreadStart));
}

TEST_F(AsyncFrameCleanupTest, TheInlineCompletionRecursionCollapsesToItsUserFrames)
{
    // Left alone this chain recurses deep enough to fill the whole 1024-frame callstack
    // budget, which costs the outermost frames -- including any stitched scope frame.
    std::vector<std::string> stack{CompressionBody};
    for (int i = 0; i < 200; i++)
    {
        stack.emplace_back("System.Threading.Tasks!Task.RunContinuations");
        stack.emplace_back("System.Threading.Tasks!UnwrapPromise<System.Boolean>.Invoke");
        stack.emplace_back("System.Threading.Tasks!UnwrapPromise<System.Boolean>.TrySetFromTask");
    }
    stack.emplace_back(ThreadStart);

    EXPECT_THAT(Clean(stack), ::testing::ElementsAre(CompressionKickoff, ThreadStart));
}

TEST_F(AsyncFrameCleanupTest, GenuineAsyncRecursionIsNotCollapsed)
{
    // Two state machine bodies of the same method really are both on the stack, so the
    // recursion depth must survive: only a kickoff frame is ever folded away.
    EXPECT_THAT(Clean({CompressionBody, CompressionBody, SessionInvoke}),
                ::testing::ElementsAre(CompressionKickoff, CompressionKickoff, SessionInvoke));
}

TEST_F(AsyncFrameCleanupTest, AFrameTheJitInlinedAwayIsNotRecovered)
{
    // The boundary of what cleanup can do. When the JIT inlined a real frame out of one
    // sample and not another, the two paths still differ afterwards -- the profiler has
    // no way to put back a frame that was never on the stack.
    EXPECT_NE(Clean({CompressionBody, SessionInvoke, HostFiltering, ThreadStart}),
              Clean({CompressionBody, SessionInvoke, ThreadStart}));
}

TEST_F(AsyncFrameCleanupTest, StitchedScopeFramesSurviveCleanup)
{
    AsyncScopeStore scopeStore;
    auto const endpoint = scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");

    // The scope frames are appended past the physical root, so they must be the last
    // thing in the stack and must never be mistaken for machinery.
    EXPECT_THAT(Clean({CompressionBody, Box, Dispatch}, &scopeStore, endpoint),
                ::testing::ElementsAre(CompressionKickoff, "GET /folders"));
}

TEST_F(AsyncFrameCleanupTest, AStackOfNothingButPlumbingKeepsItsLeaf)
{
    // Rare but real: the inline-completion recursion can fill the callstack budget and
    // push every user frame off the outer end, leaving a sample that is machinery all
    // the way down. Dropping every frame would move that time silently to the root, so
    // the leaf is kept -- that is where the sample actually was.
    EXPECT_THAT(Clean({Box, Dispatch, RunFromPool}), ::testing::ElementsAre(Box));
}
