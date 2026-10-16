#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "IFrameStore.h"

// Interns the logical async stacks that managed code pushes as it enters a scope (an HTTP
// endpoint, an OpenTelemetry span, a background job).
//
// The managed side keeps the current scope chain in an AsyncLocal, which survives an `await`,
// and republishes the chain's id onto whichever thread resumes the flow (see
// ManagedThreadInfo::SetAsyncScopeId). Samples carry that id, and RawSampleTransformer appends
// the chain's frames past the physical root, re-rooting the continuation under its logical
// parent.
//
// Chains are interned by (parent, name), so the store holds one node per distinct scope path
// rather than one per request. Names are handed back as FrameInfoView over storage owned for
// the store's lifetime, which is the same contract IFrameStore has for real frames.
class AsyncScopeStore
{
public:
    // Reserved id meaning "this sample has no logical async parent".
    static constexpr std::uint32_t NoScope = 0;

    // Past this depth Push stops deepening the chain and returns the parent instead.
    static constexpr std::uint32_t MaxDepth = 32;

    // An application can push unbounded scope names (a user id, a URL with ids in it). At the
    // cap, Push returns the parent id, so profiles degrade to coarser scopes rather than the
    // store growing without bound.
    static constexpr std::size_t DefaultMaxScopes = 8192;

    explicit AsyncScopeStore(std::size_t maxScopes = DefaultMaxScopes);

    AsyncScopeStore(AsyncScopeStore const&) = delete;
    AsyncScopeStore& operator=(AsyncScopeStore const&) = delete;

    // Interns `name` as a child of `parentId` and returns the child's id. Returns `parentId`
    // when the name is empty, the depth cap is reached or the store is full, and NoScope when
    // `parentId` is not a known id.
    std::uint32_t Push(std::uint32_t parentId, std::string_view name);

    // Copies the names of scope `id` and its ancestors into `names`, innermost scope first, and
    // returns how many were written. Returns 0 for NoScope and for unknown ids.
    std::size_t GetChain(std::uint32_t id, std::array<std::string_view, MaxDepth>& names) const;

    // Invokes `sink(FrameInfoView)` for each frame of scope `id`, innermost scope first, so that
    // the frames extend a leaf-first callstack towards its logical root. The sink runs outside
    // the store's lock. Returns how many frames were passed to the sink; zero means the id
    // resolved to nothing, either because it was NoScope or because it is stale.
    template <typename TSink>
    std::size_t ForEachFrame(std::uint32_t id, TSink&& sink) const
    {
        std::array<std::string_view, MaxDepth> names;
        auto const count = GetChain(id, names);
        for (std::size_t i = 0; i < count; i++)
        {
            sink(FrameInfoView{{}, names[i], {}, 0});
        }

        return count;
    }

    std::size_t GetScopeCount() const;
    bool IsAtCapacity() const;
    std::size_t GetMemorySize() const;

private:
    struct Scope
    {
        std::uint32_t ParentId;
        std::uint32_t Depth;
        // Interned in _names; stable for the store's lifetime because a deque never moves the
        // elements it already holds.
        std::string_view Name;
    };

    static std::string BuildKey(std::uint32_t parentId, std::string_view name);

    mutable std::shared_mutex _mutex;
    const std::size_t _maxScopes;
    bool _capacityReported{false};
    // _scopes[0] is the NoScope sentinel so that an id can be used as an index.
    std::deque<Scope> _scopes;
    std::deque<std::string> _names;
    std::unordered_map<std::string, std::uint32_t> _idsByKey;
};
