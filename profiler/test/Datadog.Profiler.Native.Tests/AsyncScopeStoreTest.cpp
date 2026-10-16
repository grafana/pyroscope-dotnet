#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "AsyncScopeStore.h"

#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::string> GetFrameNames(AsyncScopeStore const& store, std::uint32_t id)
{
    std::vector<std::string> names;
    store.ForEachFrame(id, [&names](FrameInfoView const& frame) {
        names.emplace_back(frame.Frame);
        // The synthetic frames carry no module/file: they name a scope, not code.
        EXPECT_TRUE(frame.ModuleName.empty());
        EXPECT_TRUE(frame.Filename.empty());
        EXPECT_EQ(0u, frame.StartLine);
    });
    return names;
}

} // namespace

TEST(AsyncScopeStoreTest, PushReturnsANonZeroIdForARootScope)
{
    AsyncScopeStore store;

    auto const id = store.Push(AsyncScopeStore::NoScope, "GET /folders");

    EXPECT_NE(AsyncScopeStore::NoScope, id);
    EXPECT_EQ(1u, store.GetScopeCount());
}

TEST(AsyncScopeStoreTest, PushInternsTheSameScopePathToTheSameId)
{
    AsyncScopeStore store;

    auto const first = store.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const second = store.Push(AsyncScopeStore::NoScope, "GET /folders");

    EXPECT_EQ(first, second);
    // One node per distinct scope path, not one per push: this is what keeps the
    // store bounded by code paths rather than by request count.
    EXPECT_EQ(1u, store.GetScopeCount());
}

TEST(AsyncScopeStoreTest, TheSameNameUnderDifferentParentsGetsDifferentIds)
{
    AsyncScopeStore store;

    auto const folders = store.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const documents = store.Push(AsyncScopeStore::NoScope, "GET /documents");

    auto const queryUnderFolders = store.Push(folders, "DbCommand");
    auto const queryUnderDocuments = store.Push(documents, "DbCommand");

    EXPECT_NE(queryUnderFolders, queryUnderDocuments);
    EXPECT_THAT(GetFrameNames(store, queryUnderFolders),
                ::testing::ElementsAre("DbCommand", "GET /folders"));
    EXPECT_THAT(GetFrameNames(store, queryUnderDocuments),
                ::testing::ElementsAre("DbCommand", "GET /documents"));
}

TEST(AsyncScopeStoreTest, FramesAreInnermostFirstSoTheyExtendALeafFirstCallstack)
{
    AsyncScopeStore store;

    auto const endpoint = store.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const service = store.Push(endpoint, "FolderService.Load");
    auto const query = store.Push(service, "DbCommand");

    EXPECT_THAT(GetFrameNames(store, query),
                ::testing::ElementsAre("DbCommand", "FolderService.Load", "GET /folders"));
}

TEST(AsyncScopeStoreTest, NoScopeHasNoFrames)
{
    AsyncScopeStore store;
    store.Push(AsyncScopeStore::NoScope, "GET /folders");

    EXPECT_THAT(GetFrameNames(store, AsyncScopeStore::NoScope), ::testing::IsEmpty());
}

TEST(AsyncScopeStoreTest, AnUnknownIdHasNoFrames)
{
    AsyncScopeStore store;
    auto const known = store.Push(AsyncScopeStore::NoScope, "GET /folders");

    EXPECT_THAT(GetFrameNames(store, known + 1000), ::testing::IsEmpty());
}

TEST(AsyncScopeStoreTest, AnEmptyNameKeepsTheParentScope)
{
    AsyncScopeStore store;
    auto const parent = store.Push(AsyncScopeStore::NoScope, "GET /folders");

    EXPECT_EQ(parent, store.Push(parent, ""));
    EXPECT_EQ(1u, store.GetScopeCount());
}

TEST(AsyncScopeStoreTest, AnUnknownParentYieldsNoScopeRatherThanAWrongParent)
{
    AsyncScopeStore store;

    // Attaching to an id the store does not know would silently root the work under
    // an unrelated scope, so the chain is dropped instead.
    EXPECT_EQ(AsyncScopeStore::NoScope, store.Push(999, "DbCommand"));
    EXPECT_EQ(0u, store.GetScopeCount());
}

TEST(AsyncScopeStoreTest, ChainsStopDeepeningAtMaxDepth)
{
    AsyncScopeStore store;

    std::uint32_t id = AsyncScopeStore::NoScope;
    for (std::uint32_t i = 0; i < AsyncScopeStore::MaxDepth; i++)
    {
        id = store.Push(id, "scope" + std::to_string(i));
        ASSERT_NE(AsyncScopeStore::NoScope, id);
    }

    // At the cap the deepest scope is kept and the new one is dropped, so the work
    // stays attributed to the enclosing chain.
    auto const tooDeep = store.Push(id, "one too many");
    EXPECT_EQ(id, tooDeep);
    EXPECT_EQ(AsyncScopeStore::MaxDepth, store.GetScopeCount());
    EXPECT_EQ(AsyncScopeStore::MaxDepth, GetFrameNames(store, id).size());
}

TEST(AsyncScopeStoreTest, PushStopsInterningAtCapacityAndReportsIt)
{
    AsyncScopeStore store(4);

    std::vector<std::uint32_t> ids;
    for (auto i = 0; i < 4; i++)
    {
        EXPECT_FALSE(store.IsAtCapacity());
        ids.push_back(store.Push(AsyncScopeStore::NoScope, "scope" + std::to_string(i)));
        ASSERT_NE(AsyncScopeStore::NoScope, ids.back());
    }

    // A fifth distinct root scope does not fit; an application pushing unbounded
    // names degrades to its parent scope instead of growing the store.
    EXPECT_EQ(AsyncScopeStore::NoScope, store.Push(AsyncScopeStore::NoScope, "scope4"));
    EXPECT_TRUE(store.IsAtCapacity());
    EXPECT_EQ(4u, store.GetScopeCount());

    // Already-interned paths keep working after the cap is hit.
    EXPECT_EQ(ids[0], store.Push(AsyncScopeStore::NoScope, "scope0"));
}

TEST(AsyncScopeStoreTest, ConcurrentPushesOfTheSamePathAgreeOnOneId)
{
    AsyncScopeStore store;
    auto const parent = store.Push(AsyncScopeStore::NoScope, "GET /folders");

    constexpr int threadCount = 8;
    constexpr int pushesPerThread = 200;

    std::atomic<bool> start{false};
    std::vector<std::vector<std::uint32_t>> results(threadCount);
    std::vector<std::thread> threads;

    for (auto t = 0; t < threadCount; t++)
    {
        threads.emplace_back([&store, &start, &results, parent, t]() {
            while (!start.load())
            {
            }
            for (auto i = 0; i < pushesPerThread; i++)
            {
                // Half the threads race on one shared path, the rest add their own, so
                // the test covers both the cache-hit and the interning paths.
                results[t].push_back(t % 2 == 0
                                         ? store.Push(parent, "DbCommand")
                                         : store.Push(parent, "DbCommand" + std::to_string(t)));
            }
        });
    }

    start.store(true);
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::set<std::uint32_t> sharedIds;
    for (auto t = 0; t < threadCount; t++)
    {
        std::set<std::uint32_t> distinct(results[t].begin(), results[t].end());
        ASSERT_EQ(1u, distinct.size()) << "thread " << t << " saw more than one id for its path";
        if (t % 2 == 0)
        {
            sharedIds.insert(*distinct.begin());
        }
    }

    EXPECT_EQ(1u, sharedIds.size());
    // "GET /folders" + "DbCommand" + one per odd thread.
    EXPECT_EQ(2u + threadCount / 2, store.GetScopeCount());
}

TEST(AsyncScopeStoreTest, ConcurrentReadsSeeCompleteChainsWhileScopesAreAdded)
{
    AsyncScopeStore store;
    auto const endpoint = store.Push(AsyncScopeStore::NoScope, "GET /folders");
    auto const nested = store.Push(endpoint, "FolderService.Load");

    std::atomic<bool> stop{false};
    std::thread writer([&store, endpoint, &stop]() {
        for (auto i = 0; i < 2000 && !stop.load(); i++)
        {
            store.Push(endpoint, "churn" + std::to_string(i));
        }
        stop.store(true);
    });

    // Reading a chain must not tear while other threads intern new scopes: this is the
    // pattern the exporter thread hits while request threads push scopes.
    for (auto i = 0; i < 2000 || !stop.load(); i++)
    {
        EXPECT_THAT(GetFrameNames(store, nested),
                    ::testing::ElementsAre("FolderService.Load", "GET /folders"));
    }

    writer.join();
}
