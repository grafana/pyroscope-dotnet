#include "AsyncScopeStore.h"

#include "Log.h"

#include <mutex>

AsyncScopeStore::AsyncScopeStore(std::size_t maxScopes) :
    _maxScopes{maxScopes}
{
    // The sentinel at index 0 keeps `id` usable as a direct index into _scopes.
    _scopes.push_back(Scope{NoScope, 0, {}});
}

std::string AsyncScopeStore::BuildKey(std::uint32_t parentId, std::string_view name)
{
    // The separator cannot occur in the fixed-width parent id, so (parent, name)
    // pairs cannot collide.
    std::string key;
    key.reserve(sizeof(parentId) + 1 + name.size());
    key.append(reinterpret_cast<const char*>(&parentId), sizeof(parentId));
    key.push_back('\0');
    key.append(name);
    return key;
}

std::uint32_t AsyncScopeStore::Push(std::uint32_t parentId, std::string_view name)
{
    if (name.empty())
    {
        return parentId;
    }

    {
        std::shared_lock lock(_mutex);

        if (parentId >= _scopes.size())
        {
            // A stale id from a previous store instance, or a caller bug: drop the
            // chain rather than attaching this scope to an unrelated parent.
            return NoScope;
        }

        if (_scopes[parentId].Depth >= MaxDepth)
        {
            return parentId;
        }
    }

    auto key = BuildKey(parentId, name);

    {
        std::shared_lock lock(_mutex);
        auto it = _idsByKey.find(key);
        if (it != _idsByKey.end())
        {
            return it->second;
        }
    }

    std::unique_lock lock(_mutex);

    // Re-check: another thread may have interned the same scope while we were
    // upgrading from the shared lock.
    auto it = _idsByKey.find(key);
    if (it != _idsByKey.end())
    {
        return it->second;
    }

    if (parentId >= _scopes.size())
    {
        return NoScope;
    }

    if (_scopes.size() - 1 >= _maxScopes) // -1: the NoScope sentinel is not a scope
    {
        // Almost always means scope names carry something unbounded where a route template was
        // intended, so it is worth telling the operator about.
        if (!_capacityReported)
        {
            _capacityReported = true;
            Log::Warn("Async scope store is full (", _maxScopes,
                      " scopes): further scope names will be attributed to their parent scope. ",
                      "Use scope names with bounded cardinality.");
        }
        return parentId;
    }

    _names.emplace_back(name);
    auto const id = static_cast<std::uint32_t>(_scopes.size());
    _scopes.push_back(Scope{parentId, _scopes[parentId].Depth + 1, std::string_view(_names.back())});
    _idsByKey.emplace(std::move(key), id);

    return id;
}

std::size_t AsyncScopeStore::GetChain(std::uint32_t id, std::array<std::string_view, MaxDepth>& names) const
{
    if (id == NoScope)
    {
        return 0;
    }

    std::shared_lock lock(_mutex);

    std::size_t count = 0;
    // MaxDepth also bounds a malformed chain: even if a parent link were wrong,
    // this cannot loop forever.
    while (count < MaxDepth && id != NoScope && id < _scopes.size())
    {
        auto const& scope = _scopes[id];
        names[count++] = scope.Name;
        id = scope.ParentId;
    }
    return count;
}

std::size_t AsyncScopeStore::GetScopeCount() const
{
    std::shared_lock lock(_mutex);
    return _scopes.size() - 1; // exclude the NoScope sentinel
}

bool AsyncScopeStore::IsAtCapacity() const
{
    std::shared_lock lock(_mutex);
    return _scopes.size() - 1 >= _maxScopes;
}

std::size_t AsyncScopeStore::GetMemorySize() const
{
    std::shared_lock lock(_mutex);

    std::size_t size = _scopes.size() * sizeof(Scope);
    for (auto const& name : _names)
    {
        // The key stored in _idsByKey is the name plus the parent id and separator.
        size += 2 * name.size() + sizeof(std::string) + sizeof(std::uint32_t) + 1;
    }
    return size;
}
