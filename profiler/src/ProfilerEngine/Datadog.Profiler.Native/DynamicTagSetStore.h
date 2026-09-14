#pragma once

#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "JavaProfilerTags.h"

/// Interns whole sets of dynamic tags so that applying one to a thread is cheap.
///
/// Dynamic tags live per OS thread (ManagedThreadInfo::GetTags), which is why they used to
/// vanish across `await`: the continuation resumes on a thread that was never told about
/// them. The managed side fixes that by keeping the current label set in an AsyncLocal and
/// re-applying it on whichever thread resumes the flow -- but that turns tag application
/// into a hot path, and the per-key route is expensive there: google::javaprofiler::Tags::Set
/// takes a process-wide mutex to resolve the key id, and interning the value string takes
/// another. Doing that several times per `await` would serialize every continuation in the
/// application on those two locks.
///
/// So the expensive part happens once. A set is interned by content into a Tags instance,
/// and applying it afterwards is a plain Tags assignment: 16 AsyncRefCountedString copies,
/// each an atomic refcount increment. Releases stay lock-free too, because the interned set
/// keeps a permanent reference to every string it holds, so no refcount reaches zero.
///
/// Assignment is also friendlier to the sampler than the per-key route: it never leaves the
/// thread's tags momentarily empty the way ClearAll() followed by N Set() calls does.
class DynamicTagSetStore
{
public:
    // Reserved id meaning "this thread has no dynamic tags".
    static constexpr std::uint32_t NoTagSet = 0;

    // An application can build tag sets from unbounded values (a user id, a request path
    // with ids in it). At the cap Intern fails, and the managed side falls back to applying
    // the set key by key -- correct, just as slow as it was before interning existed.
    static constexpr std::size_t DefaultMaxTagSets = 16384;

    explicit DynamicTagSetStore(std::size_t maxTagSets = DefaultMaxTagSets);

    DynamicTagSetStore(DynamicTagSetStore const&) = delete;
    DynamicTagSetStore& operator=(DynamicTagSetStore const&) = delete;

    /// Interns the `count` (key, value) pairs and returns the set's id, or NoTagSet when the
    /// set is empty, a key could not be registered (Tags allows 16 distinct keys per
    /// process) or the store is full.
    std::uint32_t Intern(const char* const* keys, const char* const* values, std::size_t count);

    /// Replaces `target` with the interned set `id`, or empties it for NoTagSet.
    /// Returns false and leaves `target` untouched when `id` is unknown.
    bool ApplyTo(std::uint32_t id, google::javaprofiler::Tags& target) const;

    std::size_t GetTagSetCount() const;
    bool IsAtCapacity() const;
    std::size_t GetMemorySize() const;

private:
    static std::string BuildKey(const char* const* keys, const char* const* values, std::size_t count);

    mutable std::shared_mutex _mutex;
    const std::size_t _maxTagSets;
    bool _capacityReported{false};
    bool _keyLimitReported{false};
    // _tagSets[0] is the NoTagSet sentinel so that an id can be used as an index.
    std::deque<google::javaprofiler::Tags> _tagSets;
    std::unordered_map<std::string, std::uint32_t> _idsByKey;
};
