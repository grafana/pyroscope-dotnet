using System.Collections.Concurrent;

namespace Pyroscope
{
    // Holds the profiler context of the current async flow -- the logical scope chain, the
    // dynamic label set and the span identity -- and mirrors it onto whichever OS thread the
    // profiler will sample next.
    //
    // The profiler's own state is per thread, so it does not survive an `await`; the
    // ExecutionContext does. Registering a change handler on an AsyncLocal makes the CLR invoke
    // us on the resuming thread every time it restores an ExecutionContext, which is when we
    // publish to the per-thread slots. The same notification fires with an empty value when the
    // pool resets the thread, taking the context back off it.
    internal sealed class ProfilingContext
    {
        // Matches AsyncScopeStore::MaxDepth. Past this the chain stops deepening.
        private const int MaxScopeDepth = 32;

        // Ceilings on the interning caches. An application that pushes unbounded names or
        // label values stops being cached rather than growing these dictionaries without
        // bound; the native stores apply their own caps as well.
        private const int MaxCachedScopes = 4096;
        private const int MaxCachedTagSets = 4096;

        // What this thread's profiler slots currently hold. The slots are per thread, so caching
        // per thread lets any number of notifying AsyncLocals share a single publish: whichever
        // handler runs first writes, and the rest find nothing left to do. `t_owner` guards
        // against a second ProfilingContext (only tests build one) reading this thread's cache.
        [ThreadStatic] private static ProfilingContext? t_owner;
        [ThreadStatic] private static uint t_scopeId;
        [ThreadStatic] private static uint t_tagSetId;
        [ThreadStatic] private static SpanContext t_span;

        private readonly IProfilingContextSink _sink;

        // One AsyncLocal for all three pieces of context, not one each: the CLR fires a change
        // notification per notifying AsyncLocal on every ExecutionContext switch. Holding an
        // immutable snapshot also makes closing scopes order-independent, so ending a span
        // inside an open label scope cannot drop the labels.
        private readonly AsyncLocal<Snapshot?> _current;
        private readonly ConcurrentDictionary<ScopeKey, uint> _scopeIds = new();
        private readonly ConcurrentDictionary<string, uint> _tagSetIds = new();

        // Set once the sink has thrown (no native profiler loaded, or one too old to export the
        // entry points). After that we stop calling into it: the change handler runs on every
        // ExecutionContext switch, so a throwing P/Invoke there would be both expensive and noisy.
        private volatile bool _sinkFailed;

        public ProfilingContext(IProfilingContextSink sink, bool stitchingEnabled, bool propagationEnabled)
        {
            _sink = sink;
            IsStitchingEnabled = stitchingEnabled;
            IsPropagationEnabled = propagationEnabled;

            // Only register a change handler if something is going to be published: an AsyncLocal
            // without one costs the runtime nothing at a context switch.
            _current = stitchingEnabled || propagationEnabled
                ? new AsyncLocal<Snapshot?>(_ => Publish())
                : new AsyncLocal<Snapshot?>();
        }

        public bool IsStitchingEnabled { get; }

        public bool IsPropagationEnabled { get; }

        internal AsyncScopeNode? CurrentScope => _current.Value?.Scope;

        internal LabelSet? CurrentLabels => _current.Value?.Labels;

        internal SpanContext CurrentSpan => _current.Value?.Span ?? SpanContext.Zero;

        // ----- async scopes -------------------------------------------------------------

        // Enters an async scope below the current one. Returns the node that was pushed (null
        // when nothing was pushed) and the value to restore.
        internal (AsyncScopeNode? Pushed, AsyncScopeNode? Previous) PushScope(string? name)
        {
            var previous = CurrentScope;

            if (!IsStitchingEnabled || string.IsNullOrEmpty(name))
            {
                return (null, previous);
            }

            if (previous != null && previous.Depth >= MaxScopeDepth)
            {
                return (null, previous);
            }

            var pushed = new AsyncScopeNode(previous, name!, InternScope(previous?.NativeId ?? 0, name!));

            SetScope(pushed);

            // The scope has to be live for the code that follows, not only for continuations.
            // Assigning the AsyncLocal usually notifies us, but it short-circuits when the value
            // is unchanged, so publish here rather than depending on that.
            Publish();

            return (pushed, previous);
        }

        internal void RestoreScope(AsyncScopeNode? pushed, AsyncScopeNode? previous)
        {
            if (pushed == null)
            {
                return;
            }

            // Only unwind if this flow is still inside the scope we pushed. A scope can be closed
            // from a different async flow than the one that opened it (an OpenTelemetry span
            // ended by a callback, say), and restoring there would attribute the rest of that
            // flow to a scope it never entered.
            for (var node = CurrentScope; node != null; node = node.Parent)
            {
                if (ReferenceEquals(node, pushed))
                {
                    SetScope(previous);
                    Publish();
                    return;
                }
            }
        }

        // ----- dynamic labels -----------------------------------------------------------

        // Makes `labels` the ambient label set. Returns the set that was replaced, for the
        // caller to restore.
        internal LabelSet? PushLabels(LabelSet? labels)
        {
            var previous = CurrentLabels;

            // Resolve the id here rather than letting the change handler do it: interning
            // marshals an array, and the handler runs inside the runtime's context switch.
            // Skipped when propagation is off, since the set is not going to be published.
            if (IsPropagationEnabled && labels != null && labels.Count > 0)
            {
                ResolveTagSetId(labels);
            }

            SetLabels(labels);
            Publish();
            ApplyLabelsDirectlyIfNeeded(labels);
            return previous;
        }

        internal void RestoreLabels(LabelSet? pushed, LabelSet? previous)
        {
            // Same reasoning as RestoreScope: leave another flow's labels alone.
            if (!ReferenceEquals(CurrentLabels, pushed))
            {
                return;
            }

            SetLabels(previous);
            Publish();
            ApplyLabelsDirectlyIfNeeded(previous);
        }

        // Applies labels straight onto the calling thread for the cases where Publish does not
        // own the thread's tags: propagation is switched off, or this particular set can never
        // be interned. Either way the labels cover the synchronous part of the flow and stop at
        // the first `await`, which is what the profiler did before propagation existed.
        private void ApplyLabelsDirectlyIfNeeded(LabelSet? labels)
        {
            if (_sinkFailed)
            {
                return;
            }

            bool ownsThreadTags;
            if (!IsPropagationEnabled)
            {
                ownsThreadTags = true;
            }
            else if (labels == null || labels.Count == 0)
            {
                // Publish already cleared them for us.
                ownsThreadTags = false;
            }
            else
            {
                ownsThreadTags = ResolveTagSetId(labels) == ProfilingContextSink.NeverInterned;
            }

            if (!ownsThreadTags)
            {
                return;
            }

            try
            {
                // Clear first: the publish left the thread's tags untouched, so whatever the set
                // being replaced put there is still present.
                _sink.ClearDynamicTags();

                if (labels == null)
                {
                    return;
                }

                foreach (var pair in labels.Labels)
                {
                    _sink.SetDynamicTag(pair.Key, pair.Value);
                }
            }
            catch (Exception ex)
            {
                ReportSinkFailure(ex);
            }
        }

        // ----- span context -------------------------------------------------------------

        // Makes `context` the ambient span identity. Returns the one it replaced.
        internal SpanContext PushSpan(SpanContext context)
        {
            var previous = CurrentSpan;
            SetSpan(context);

            if (IsPropagationEnabled)
            {
                Publish();
            }
            else
            {
                // No change handler to do it for us; write it where it used to go.
                PublishSpanDirectly(context);
            }

            return previous;
        }

        internal void RestoreSpan(SpanContext previous)
        {
            SetSpan(previous);

            if (IsPropagationEnabled)
            {
                Publish();
            }
            else
            {
                PublishSpanDirectly(previous);
            }
        }

        private void PublishSpanDirectly(SpanContext context)
        {
            if (_sinkFailed)
            {
                return;
            }

            try
            {
                _sink.SetSpanContext(context);
            }
            catch (Exception ex)
            {
                ReportSinkFailure(ex);
            }
        }

        // ----- publishing ---------------------------------------------------------------

        // Writes the current flow's context into the calling thread's profiler slots. Runs on
        // whichever thread the CLR is installing or removing an ExecutionContext on -- for a
        // thread-pool continuation, the thread that is about to run it.
        private void Publish()
        {
            if (_sinkFailed)
            {
                return;
            }

            try
            {
                // The AsyncLocal already reflects the incoming context by the time the change
                // handler runs, so one read publishes the whole thing.
                var snapshot = _current.Value;

                var scopeId = IsStitchingEnabled ? snapshot?.Scope?.NativeId ?? 0 : 0;

                // NeverInterned means "leave this thread's tags alone": with propagation off,
                // labels are applied directly by PushLabels and must not be overwritten here.
                var tagSetId = ProfilingContextSink.NeverInterned;
                if (IsPropagationEnabled)
                {
                    var labels = snapshot?.Labels;
                    tagSetId = labels == null || labels.Count == 0 ? 0u : ResolveTagSetId(labels);
                }

                var span = IsPropagationEnabled ? snapshot?.Span ?? SpanContext.Zero : SpanContext.Zero;

                var stale = !ReferenceEquals(t_owner, this);

                if (stale || scopeId != t_scopeId || tagSetId != t_tagSetId)
                {
                    _sink.SetCurrentProfilingContext(scopeId, tagSetId);
                    t_scopeId = scopeId;
                    t_tagSetId = tagSetId;
                }

                if (IsPropagationEnabled && (stale || !span.Equals(t_span)))
                {
                    _sink.SetSpanContext(span);
                    t_span = span;
                }

                t_owner = this;
            }

            // Catch everything, and do not narrow this: the CLR wraps the AsyncLocal
            // change-notification fan-out in a try/catch that ends in Environment.FailFast
            // (ExecutionContext.OnValuesChanged), so an exception escaping here takes the
            // process down.
            catch (Exception ex)
            {
                ReportSinkFailure(ex);
            }
        }

        // Stops calling the sink after it has failed once, and says why: otherwise the feature
        // quietly stops working, which is hard to diagnose from a profile that is merely missing
        // labels.
        private void ReportSinkFailure(Exception ex)
        {
            if (_sinkFailed)
            {
                return;
            }

            _sinkFailed = true;
            Console.WriteLine(
                "[Pyroscope] Async context propagation is disabled for this process: the profiler " +
                $"rejected a call ({ex.GetType().Name}: {ex.Message}). Labels and async scopes will " +
                "only apply to the thread that sets them.");
        }

        private uint InternScope(uint parentId, string name)
        {
            if (_sinkFailed)
            {
                // Keep the parent's id so an inner scope we cannot record does not detach the work
                // from the endpoint above it.
                return parentId;
            }

            var key = new ScopeKey(parentId, name);
            if (_scopeIds.TryGetValue(key, out var cached))
            {
                return cached;
            }

            uint id;
            try
            {
                id = _sink.InternAsyncScope(parentId, name);
            }
            catch (Exception ex)
            {
                ReportSinkFailure(ex);
                return parentId;
            }

            if (id == 0)
            {
                // The profiler has not finished attaching. Deliberately not cached: a cold start
                // pushes scopes before it is ready, and those paths must pick up an id once it is.
                return 0;
            }

            if (_scopeIds.Count < MaxCachedScopes)
            {
                _scopeIds.TryAdd(key, id);
            }

            return id;
        }

        private uint ResolveTagSetId(LabelSet labels)
        {
            if (_sinkFailed)
            {
                return ProfilingContextSink.NeverInterned;
            }

            // Resolved once per LabelSet instance, so a request that reuses its set pays nothing
            // after the first publish, then once per distinct content, so a set rebuilt for every
            // request costs one dictionary lookup rather than a native round trip.
            var resolved = labels.NativeTagSetId;
            if (resolved.HasValue)
            {
                return resolved.Value;
            }

            if (labels.Count == 0)
            {
                labels.NativeTagSetId = 0;
                return 0;
            }

            var contentKey = labels.ContentKey;
            if (_tagSetIds.TryGetValue(contentKey, out var cachedId))
            {
                labels.NativeTagSetId = cachedId;
                return cachedId;
            }

            uint id;
            try
            {
                id = _sink.InternDynamicTagSet(labels.Keys, labels.Values);
            }
            catch (Exception ex)
            {
                ReportSinkFailure(ex);
                return ProfilingContextSink.NeverInterned;
            }

            if (id == 0)
            {
                // Still starting up; try again next time rather than caching the miss.
                return 0;
            }

            if (_tagSetIds.Count < MaxCachedTagSets)
            {
                _tagSetIds.TryAdd(contentKey, id);
            }

            labels.NativeTagSetId = id;
            return id;
        }

        private void SetScope(AsyncScopeNode? scope) => Set(Snapshot.Current(_current).WithScope(scope));

        private void SetLabels(LabelSet? labels) => Set(Snapshot.Current(_current).WithLabels(labels));

        private void SetSpan(SpanContext span) => Set(Snapshot.Current(_current).WithSpan(span));

        private void Set(Snapshot snapshot)
        {
            // Drop the entry entirely once nothing is left, so an ExecutionContext that no longer
            // carries any profiler context does not keep a value alive for it.
            _current.Value = snapshot.IsEmpty ? null : snapshot;
        }

        // The three pieces of context, immutable so that every async flow branching off a scope
        // can share one instance, and so that a field can be replaced without disturbing the
        // others.
        private sealed class Snapshot
        {
            private static readonly Snapshot Empty = new(null, null, SpanContext.Zero);

            private Snapshot(AsyncScopeNode? scope, LabelSet? labels, SpanContext span)
            {
                Scope = scope;
                Labels = labels;
                Span = span;
            }

            public AsyncScopeNode? Scope { get; }

            public LabelSet? Labels { get; }

            public SpanContext Span { get; }

            public bool IsEmpty => Scope == null && Labels == null && Span.IsZero;

            public static Snapshot Current(AsyncLocal<Snapshot?> local) => local.Value ?? Empty;

            public Snapshot WithScope(AsyncScopeNode? scope) => new(scope, Labels, Span);

            public Snapshot WithLabels(LabelSet? labels) => new(Scope, labels, Span);

            public Snapshot WithSpan(SpanContext span) => new(Scope, Labels, span);
        }

        private readonly struct ScopeKey : IEquatable<ScopeKey>
        {
            private readonly uint _parentId;
            private readonly string _name;

            public ScopeKey(uint parentId, string name)
            {
                _parentId = parentId;
                _name = name;
            }

            public bool Equals(ScopeKey other) => _parentId == other._parentId && _name == other._name;

            public override bool Equals(object? obj) => obj is ScopeKey other && Equals(other);

            public override int GetHashCode() => unchecked(((int)_parentId * 397) ^ _name.GetHashCode());
        }
    }
}
