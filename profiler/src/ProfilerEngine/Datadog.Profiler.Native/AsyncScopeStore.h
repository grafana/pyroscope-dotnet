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

/// Interns the logical async stacks that managed code pushes as it enters a scope
/// (an HTTP endpoint, an OpenTelemetry span, a background job).
///
/// A stack-sampling profiler loses the logical caller across `await`: the
/// continuation resumes on a thread-pool thread whose physical stack is rooted at
/// ThreadPoolWorkQueue.Dispatch -> AsyncStateMachineBox.MoveNext, and the method
/// that awaited is nowhere on it. What *does* survive an await is the
/// ExecutionContext, so the managed side keeps the current scope chain in an
/// AsyncLocal and republishes the chain's id onto whichever thread resumes the
/// flow (see ManagedThreadInfo::SetAsyncScopeId). Samples carry that id, and
/// RawSampleTransformer appends the chain's frames past the physical root, which
/// re-roots the continuation under its logical parent.
///
/// Chains are interned by (parent, name), so the store holds one node per
/// distinct scope *path* rather than one per request: the node count is bounded
/// by the application's code paths, much like the node count of a flamegraph.
/// Names are handed back as FrameInfoView over storage owned for the store's
/// lifetime, which is the same contract IFrameStore has for real frames.
class AsyncScopeStore
{
public:
    // Reserved id meaning "this sample has no logical async parent".
    static constexpr std::uint32_t NoScope = 0;

    // Past ~30 nested scopes the extra frames stop being informative and start
    // costing sample size, so Push stops deepening the chain and returns the
    // parent instead.
    static constexpr std::uint32_t MaxDepth = 32;

    // An application can push unbounded scope names (a user id, a URL with ids
    // in it). At the cap, Push returns the parent id, so profiles degrade to
    // coarser scopes rather than the store growing without bound.
    static constexpr std::size_t DefaultMaxScopes = 8192;

    explicit AsyncScopeStore(std::size_t maxScopes = DefaultMaxScopes);

    AsyncScopeStore(AsyncScopeStore const&) = delete;
    AsyncScopeStore& operator=(AsyncScopeStore const&) = delete;

    /// Interns `name` as a child of `parentId` and returns the child's id.
    /// Returns `parentId` when the name is empty, the depth cap is reached or the
    /// store is full, and NoScope when `parentId` is not a known id.
    std::uint32_t Push(std::uint32_t parentId, std::string_view name);

    /// Copies the names of scope `id` and its ancestors into `names`, innermost
    /// scope first, and returns how many were written. Returns 0 for NoScope and
    /// for unknown ids.
    std::size_t GetChain(std::uint32_t id, std::array<std::string_view, MaxDepth>& names) const;

    /// Invokes `sink(FrameInfoView)` for each frame of scope `id`, innermost scope
    /// first, so that the frames extend a leaf-first callstack towards its logical
    /// root. A no-op for NoScope and for unknown ids. The sink runs outside the
    /// store's lock.
    /// Returns how many frames were passed to the sink. Zero means the id resolved to
    /// nothing -- either it was NoScope, or it is stale -- which the caller counts
    /// separately so "no scopes in use" can be told apart from "scopes are being evicted".
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
        // Interned in _names; stable for the store's lifetime because a deque
        // never moves the elements it already holds.
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
