#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "DynamicTagSetStore.h"
#include "JavaProfilerTags.h"
#include "async_ref_counted_string.h"

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

// AsyncRefCountedString and Tags keep process-wide tables that must be initialized before
// any tag can be stored. ProviderTest registers the same environment; both are idempotent.
class DynamicTagSetStoreEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        google::javaprofiler::AsyncRefCountedString::Init();
        google::javaprofiler::Tags::Init();
    }
};

::testing::Environment* const tagSetEnv =
    ::testing::AddGlobalTestEnvironment(new DynamicTagSetStoreEnvironment);

using KeyValues = std::vector<std::pair<std::string, std::string>>;

std::uint32_t Intern(DynamicTagSetStore& store, KeyValues const& pairs)
{
    std::vector<const char*> keys;
    std::vector<const char*> values;
    for (auto const& pair : pairs)
    {
        keys.push_back(pair.first.c_str());
        values.push_back(pair.second.c_str());
    }
    return store.Intern(keys.data(), values.data(), pairs.size());
}

// What the exporter would see on a sample taken from a thread carrying this set.
std::map<std::string, std::string> Applied(DynamicTagSetStore const& store, std::uint32_t id)
{
    google::javaprofiler::Tags tags;
    store.ApplyTo(id, tags);

    std::map<std::string, std::string> applied;
    for (auto const& [key, value] : tags.GetAll())
    {
        applied.emplace(std::string(key), *value.Get());
    }
    tags.ClearAll();
    return applied;
}

} // namespace

TEST(DynamicTagSetStoreTest, InternReturnsANonZeroIdForANonEmptySet)
{
    DynamicTagSetStore store;

    auto const id = Intern(store, {{"vehicle", "bike"}});

    EXPECT_NE(DynamicTagSetStore::NoTagSet, id);
    EXPECT_EQ(1u, store.GetTagSetCount());
}

TEST(DynamicTagSetStoreTest, ApplyToPutsTheSetOnTheTargetTags)
{
    DynamicTagSetStore store;
    auto const id = Intern(store, {{"vehicle", "bike"}, {"driver_region", "eu-north"}});

    EXPECT_THAT(Applied(store, id),
                ::testing::UnorderedElementsAre(::testing::Pair("vehicle", "bike"),
                                                ::testing::Pair("driver_region", "eu-north")));
}

TEST(DynamicTagSetStoreTest, InternInternsTheSameContentToTheSameId)
{
    DynamicTagSetStore store;

    auto const first = Intern(store, {{"vehicle", "bike"}});
    auto const second = Intern(store, {{"vehicle", "bike"}});

    EXPECT_EQ(first, second);
    // One entry per distinct set, not one per request: this is what keeps the store bounded
    // by the label values an application actually uses.
    EXPECT_EQ(1u, store.GetTagSetCount());
}

TEST(DynamicTagSetStoreTest, DifferentValuesGetDifferentIds)
{
    DynamicTagSetStore store;

    auto const bike = Intern(store, {{"vehicle", "bike"}});
    auto const car = Intern(store, {{"vehicle", "car"}});

    EXPECT_NE(bike, car);
    EXPECT_THAT(Applied(store, bike), ::testing::ElementsAre(::testing::Pair("vehicle", "bike")));
    EXPECT_THAT(Applied(store, car), ::testing::ElementsAre(::testing::Pair("vehicle", "car")));
}

TEST(DynamicTagSetStoreTest, SetsThatOnlyDifferInWhereTheSeparatorFallsDoNotCollide)
{
    DynamicTagSetStore store;

    // Naive concatenation would make these two the same key.
    auto const first = Intern(store, {{"a", "bc"}});
    auto const second = Intern(store, {{"ab", "c"}});

    EXPECT_NE(first, second);
    EXPECT_THAT(Applied(store, first), ::testing::ElementsAre(::testing::Pair("a", "bc")));
    EXPECT_THAT(Applied(store, second), ::testing::ElementsAre(::testing::Pair("ab", "c")));
}

TEST(DynamicTagSetStoreTest, ApplyToReplacesRatherThanMergesWithWhatTheThreadHad)
{
    DynamicTagSetStore store;
    auto const withRegion = Intern(store, {{"vehicle", "bike"}, {"driver_region", "eu-north"}});
    auto const withoutRegion = Intern(store, {{"vehicle", "car"}});

    google::javaprofiler::Tags tags;
    ASSERT_TRUE(store.ApplyTo(withRegion, tags));
    ASSERT_TRUE(store.ApplyTo(withoutRegion, tags));

    // A key the previous set had must not linger, or a continuation would be labelled with
    // a dimension its own flow never set.
    std::map<std::string, std::string> applied;
    for (auto const& [key, value] : tags.GetAll())
    {
        applied.emplace(std::string(key), *value.Get());
    }
    EXPECT_THAT(applied, ::testing::ElementsAre(::testing::Pair("vehicle", "car")));
    tags.ClearAll();
}

TEST(DynamicTagSetStoreTest, NoTagSetEmptiesTheTargetTags)
{
    DynamicTagSetStore store;
    auto const id = Intern(store, {{"vehicle", "bike"}});

    google::javaprofiler::Tags tags;
    ASSERT_TRUE(store.ApplyTo(id, tags));
    ASSERT_TRUE(store.ApplyTo(DynamicTagSetStore::NoTagSet, tags));

    // This is how a thread stops advertising a flow's labels once the flow leaves it.
    EXPECT_THAT(tags.GetAll(), ::testing::IsEmpty());
}

TEST(DynamicTagSetStoreTest, AnEmptySetIsNotInterned)
{
    DynamicTagSetStore store;

    EXPECT_EQ(DynamicTagSetStore::NoTagSet, store.Intern(nullptr, nullptr, 0));
    EXPECT_EQ(DynamicTagSetStore::NoTagSet, Intern(store, {}));
    EXPECT_EQ(0u, store.GetTagSetCount());
}

TEST(DynamicTagSetStoreTest, AnUnknownIdLeavesTheTargetTagsAlone)
{
    DynamicTagSetStore store;
    auto const known = Intern(store, {{"vehicle", "bike"}});

    google::javaprofiler::Tags tags;
    ASSERT_TRUE(store.ApplyTo(known, tags));
    EXPECT_FALSE(store.ApplyTo(known + 500, tags));

    // Better to keep stale labels than to replace them with an unrelated set.
    std::map<std::string, std::string> applied;
    for (auto const& [key, value] : tags.GetAll())
    {
        applied.emplace(std::string(key), *value.Get());
    }
    EXPECT_THAT(applied, ::testing::ElementsAre(::testing::Pair("vehicle", "bike")));
    tags.ClearAll();
}

TEST(DynamicTagSetStoreTest, InternStopsAtCapacityAndReportsIt)
{
    DynamicTagSetStore store(3);

    for (auto i = 0; i < 3; i++)
    {
        EXPECT_FALSE(store.IsAtCapacity());
        ASSERT_NE(DynamicTagSetStore::NoTagSet, Intern(store, {{"vehicle", "v" + std::to_string(i)}}));
    }

    // A fourth distinct set does not fit. NoTagSet tells the managed side to fall back to
    // applying the labels one key at a time rather than dropping them.
    EXPECT_EQ(DynamicTagSetStore::NoTagSet, Intern(store, {{"vehicle", "v3"}}));
    EXPECT_TRUE(store.IsAtCapacity());
    EXPECT_EQ(3u, store.GetTagSetCount());

    // Already-interned sets keep working after the cap is hit.
    EXPECT_THAT(Applied(store, Intern(store, {{"vehicle", "v0"}})),
                ::testing::ElementsAre(::testing::Pair("vehicle", "v0")));
}

TEST(DynamicTagSetStoreTest, ConcurrentInternsOfTheSameSetAgreeOnOneId)
{
    DynamicTagSetStore store;

    constexpr int threadCount = 8;
    constexpr int internsPerThread = 200;

    std::atomic<bool> start{false};
    std::vector<std::set<std::uint32_t>> results(threadCount);
    std::vector<std::thread> threads;

    for (auto t = 0; t < threadCount; t++)
    {
        threads.emplace_back([&store, &start, &results, t]() {
            while (!start.load())
            {
            }
            for (auto i = 0; i < internsPerThread; i++)
            {
                // Half the threads race on one shared set, the rest add their own, so both
                // the cache-hit and the interning paths are covered.
                results[t].insert(t % 2 == 0
                                      ? Intern(store, {{"vehicle", "shared"}})
                                      : Intern(store, {{"vehicle", "own" + std::to_string(t)}}));
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
        ASSERT_EQ(1u, results[t].size()) << "thread " << t << " saw more than one id for its set";
        if (t % 2 == 0)
        {
            sharedIds.insert(*results[t].begin());
        }
    }

    EXPECT_EQ(1u, sharedIds.size());
    EXPECT_EQ(1u + threadCount / 2, store.GetTagSetCount());
}

TEST(DynamicTagSetStoreTest, ConcurrentApplyToSeesCompleteSetsWhileSetsAreAdded)
{
    DynamicTagSetStore store;
    auto const stable = Intern(store, {{"vehicle", "bike"}, {"driver_region", "eu-north"}});

    std::atomic<bool> stop{false};
    std::thread writer([&store, &stop]() {
        for (auto i = 0; i < 2000 && !stop.load(); i++)
        {
            Intern(store, {{"vehicle", "churn" + std::to_string(i)}});
        }
        stop.store(true);
    });

    // Applying a set must not tear while other threads intern new ones: this is the pattern
    // request threads hit while resuming continuations.
    for (auto i = 0; i < 2000 || !stop.load(); i++)
    {
        EXPECT_THAT(Applied(store, stable),
                    ::testing::UnorderedElementsAre(::testing::Pair("vehicle", "bike"),
                                                    ::testing::Pair("driver_region", "eu-north")));
    }

    writer.join();
}
