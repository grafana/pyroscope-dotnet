#include "DynamicTagSetStore.h"

#include "Log.h"

#include <mutex>

DynamicTagSetStore::DynamicTagSetStore(std::size_t maxTagSets) :
    _maxTagSets{maxTagSets}
{
    // The sentinel at index 0 keeps `id` usable as a direct index into _tagSets.
    _tagSets.emplace_back();
}

std::string DynamicTagSetStore::BuildKey(const char* const* keys, const char* const* values, std::size_t count)
{
    // Keys and values are length-prefixed so that no combination of separators in the
    // strings themselves can make two different sets collide.
    std::string key;
    for (std::size_t i = 0; i < count; i++)
    {
        for (auto const* part : {keys[i], values[i]})
        {
            std::string_view view{part == nullptr ? "" : part};
            auto const size = static_cast<std::uint32_t>(view.size());
            key.append(reinterpret_cast<const char*>(&size), sizeof(size));
            key.append(view);
        }
    }
    return key;
}

std::uint32_t DynamicTagSetStore::Intern(const char* const* keys, const char* const* values, std::size_t count)
{
    if (keys == nullptr || values == nullptr || count == 0)
    {
        return NoTagSet;
    }

    auto key = BuildKey(keys, values, count);

    {
        std::shared_lock lock(_mutex);
        auto it = _idsByKey.find(key);
        if (it != _idsByKey.end())
        {
            return it->second;
        }
    }

    std::unique_lock lock(_mutex);

    // Re-check: another thread may have interned the same set while we were upgrading from
    // the shared lock.
    auto it = _idsByKey.find(key);
    if (it != _idsByKey.end())
    {
        return it->second;
    }

    if (_tagSets.size() - 1 >= _maxTagSets) // -1: the NoTagSet sentinel is not a set
    {
        // Almost always means a label value carries something unbounded, which also means the
        // profile is producing that many series.
        if (!_capacityReported)
        {
            _capacityReported = true;
            Log::Warn("Dynamic tag set store is full (", _maxTagSets,
                      " sets): further label sets will be applied one key at a time, which is slower. ",
                      "Use label values with bounded cardinality.");
        }
        return NoTagSet;
    }

    auto& tags = _tagSets.emplace_back();
    for (std::size_t i = 0; i < count; i++)
    {
        if (keys[i] == nullptr || values[i] == nullptr)
        {
            continue;
        }

        if (!tags.Set(keys[i], google::javaprofiler::AsyncRefCountedString(values[i])))
        {
            // Tags keeps one process-wide key table of 16 entries; past that a key cannot be
            // stored at all. Drop the whole set rather than interning a partial one.
            if (!_keyLimitReported)
            {
                _keyLimitReported = true;
                Log::Warn("Cannot register dynamic tag key '", keys[i],
                          "': the profiler supports at most ", google::javaprofiler::kMaxNumTags,
                          " distinct label keys per process. Label sets using it will be ignored.");
            }
            tags.ClearAll();
            _tagSets.pop_back();
            return NoTagSet;
        }
    }

    auto const id = static_cast<std::uint32_t>(_tagSets.size() - 1);
    _idsByKey.emplace(std::move(key), id);

    return id;
}

bool DynamicTagSetStore::ApplyTo(std::uint32_t id, google::javaprofiler::Tags& target) const
{
    if (id == NoTagSet)
    {
        target.ClearAll();
        return true;
    }

    std::shared_lock lock(_mutex);

    if (id >= _tagSets.size())
    {
        // A stale id from a previous store instance, or a caller bug: leave the thread's
        // tags alone rather than replacing them with something unrelated.
        return false;
    }

    // An atomic increment per string and no lock, because this store holds a permanent
    // reference to every string in the set.
    target = _tagSets[id];
    return true;
}

std::size_t DynamicTagSetStore::GetTagSetCount() const
{
    std::shared_lock lock(_mutex);
    return _tagSets.size() - 1; // exclude the NoTagSet sentinel
}

bool DynamicTagSetStore::IsAtCapacity() const
{
    std::shared_lock lock(_mutex);
    return _tagSets.size() - 1 >= _maxTagSets;
}

std::size_t DynamicTagSetStore::GetMemorySize() const
{
    std::shared_lock lock(_mutex);

    std::size_t size = _tagSets.size() * sizeof(google::javaprofiler::Tags);
    for (auto const& entry : _idsByKey)
    {
        size += entry.first.size() + sizeof(std::string) + sizeof(std::uint32_t);
    }
    return size;
}
