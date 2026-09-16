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

// FrameStoreHelper(true, "Frame", n) resolves instruction pointers 1..n to
// "Frame #1".."Frame #n"; the sampler collects them leaf-first.
constexpr std::size_t PhysicalFrameCount = 3;

class AsyncScopeStitchingTest : public ::testing::Test
{
protected:
    AsyncScopeStitchingTest() :
        _frameStore{true, "Frame", PhysicalFrameCount},
        _appDomainStore{1},
        _callstackProvider{MemoryResourceManager::GetDefault()}
    {
        EXPECT_CALL(_runtimeIdStore, GetId(::testing::_)).WillRepeatedly(::testing::Return("rid"));
        Sample::ValuesCount = 1;
    }

    RawWallTimeSample MakeSample(std::uint32_t asyncScopeId) const
    {
        RawWallTimeSample raw;
        raw.Timestamp = 1ns;
        raw.Duration = 10ns;
        raw.AppDomainId = static_cast<AppDomainID>(1);
        raw.AsyncScopeId = asyncScopeId;
        raw.Stack = const_cast<CallstackProvider&>(_callstackProvider).Get();
        for (std::size_t i = 0; i < PhysicalFrameCount; i++)
        {
            raw.Stack.Add(i + 1);
        }
        return raw;
    }

    std::vector<std::string> GetFrameNames(RawSampleTransformer& transformer, RawWallTimeSample const& raw) const
    {
        std::vector<SampleValueTypeProvider::Offset> offsets{0};
        auto sample = transformer.Transform(raw, offsets);

        std::vector<std::string> names;
        for (auto const& frame : sample->GetCallstack())
        {
            names.emplace_back(frame.Frame);
        }
        return names;
    }

    FrameStoreHelper _frameStore;
    AppDomainStoreHelper _appDomainStore;
    MockRuntimeIdStore _runtimeIdStore;
    CallstackProvider _callstackProvider;
};

} // namespace

TEST_F(AsyncScopeStitchingTest, ScopeFramesExtendThePhysicalStackPastItsRoot)
{
    AsyncScopeStore scopeStore;
    auto const endpoint = scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const service = scopeStore.Push(endpoint, "FolderService.Load");

    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore, &scopeStore);

    // The callstack is leaf-first, so the logical parents come last: the endpoint that
    // caused this work ends up as the outermost frame, which is what re-roots an
    // `await` continuation under it instead of under ThreadPoolWorkQueue.Dispatch.
    EXPECT_THAT(GetFrameNames(transformer, MakeSample(service)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3",
                                       "FolderService.Load", "GET /folders"));
}

TEST_F(AsyncScopeStitchingTest, SamplesTakenOutsideAnyScopeKeepOnlyPhysicalFrames)
{
    AsyncScopeStore scopeStore;
    scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");

    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore, &scopeStore);

    EXPECT_THAT(GetFrameNames(transformer, MakeSample(AsyncScopeStore::NoScope)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3"));
}

TEST_F(AsyncScopeStitchingTest, WithoutAScopeStoreTheStackIsUnchanged)
{
    // This is the shape of every sample while PYROSCOPE_ASYNC_STITCHING_ENABLED is unset,
    // which is the default: physical stacks exactly as before stitching existed.
    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore);

    EXPECT_THAT(GetFrameNames(transformer, MakeSample(7)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3"));
}

TEST_F(AsyncScopeStitchingTest, TwoSamplesInDifferentScopesGetDifferentLogicalRoots)
{
    AsyncScopeStore scopeStore;
    auto const folders = scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const documents = scopeStore.Push(AsyncScopeStore::NoScope, "GET /documents");

    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore, &scopeStore);

    // Identical physical stacks -- the same thread-pool continuation code running for two
    // different requests -- must not merge into one flamegraph node.
    EXPECT_THAT(GetFrameNames(transformer, MakeSample(folders)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3", "GET /folders"));
    EXPECT_THAT(GetFrameNames(transformer, MakeSample(documents)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3", "GET /documents"));
}

TEST_F(AsyncScopeStitchingTest, CoverageCountersDistinguishStitchedFromUnstitchedAndStale)
{
    // Stitching must not fail the way PerfView's does: when it cannot find the thread-pool
    // transition it silently returns the unstitched stack (ActivityComputer.cs:1004), with
    // nothing to say so. These counters are how a deployment answers "is stitching actually
    // working here?" without eyeballing a flamegraph.
    AsyncScopeStore scopeStore;
    auto const endpoint = scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");

    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore, &scopeStore);

    // Ordinary synchronous work: counted, not stitched, and not stale.
    GetFrameNames(transformer, MakeSample(AsyncScopeStore::NoScope));
    EXPECT_EQ(1u, transformer.GetStitchingSampleCount());
    EXPECT_EQ(0u, transformer.GetStitchedSampleCount());
    EXPECT_EQ(0u, transformer.GetStaleScopeIdCount());

    // A live scope: stitched.
    GetFrameNames(transformer, MakeSample(endpoint));
    EXPECT_EQ(2u, transformer.GetStitchingSampleCount());
    EXPECT_EQ(1u, transformer.GetStitchedSampleCount());
    EXPECT_EQ(0u, transformer.GetStaleScopeIdCount());

    // An id the store never issued. Without a separate counter this is indistinguishable
    // from synchronous work, which is exactly the confusion worth avoiding: one means
    // "nothing to attribute", the other means "scopes are being evicted".
    GetFrameNames(transformer, MakeSample(endpoint + 500));
    EXPECT_EQ(3u, transformer.GetStitchingSampleCount());
    EXPECT_EQ(1u, transformer.GetStitchedSampleCount());
    EXPECT_EQ(1u, transformer.GetStaleScopeIdCount());
}

TEST_F(AsyncScopeStitchingTest, WithStitchingOffNoCoverageIsReported)
{
    // Without PYROSCOPE_ASYNC_STITCHING_ENABLED there is no scope store, so the counters must
    // stay at zero rather than reporting 100% unstitched and looking like a fault.
    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore);

    GetFrameNames(transformer, MakeSample(7));

    EXPECT_EQ(0u, transformer.GetStitchingSampleCount());
    EXPECT_EQ(0u, transformer.GetStitchedSampleCount());
    EXPECT_EQ(0u, transformer.GetStaleScopeIdCount());
}

TEST_F(AsyncScopeStitchingTest, AStaleScopeIdIsIgnoredRatherThanMisattributed)
{
    AsyncScopeStore scopeStore;
    auto const endpoint = scopeStore.Push(AsyncScopeStore::NoScope, "GET /folders");

    RawSampleTransformer transformer(&_frameStore, &_appDomainStore, &_runtimeIdStore, &scopeStore);

    EXPECT_THAT(GetFrameNames(transformer, MakeSample(endpoint + 500)),
                ::testing::ElementsAre("Frame #1", "Frame #2", "Frame #3"));
}
