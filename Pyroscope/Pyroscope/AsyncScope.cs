namespace Pyroscope
{
    /// <summary>
    /// A logical scope that the profiler attributes <c>await</c> continuations to.
    ///
    /// The profiler samples physical call stacks, so an <c>await</c> hides the caller:
    /// the continuation resumes on a thread-pool thread and its samples form a separate
    /// tree rooted at <c>ThreadPoolWorkQueue.Dispatch</c>, not a subtree of the endpoint
    /// that started the request. Wrapping the request in a scope restores the
    /// relationship -- every sample taken inside the scope, on whatever thread, gets the
    /// scope's name as a frame above its physical stack:
    ///
    /// <code>
    /// app.MapGet("/folders/{id}", async (int id, FolderService folders) =&gt;
    /// {
    ///     using (Pyroscope.AsyncScope.Push("GET /folders/{id}"))
    ///     {
    ///         return await folders.LoadAsync(id);
    ///     }
    /// });
    /// </code>
    ///
    /// Scopes nest, and the chain follows the request across awaits, so the flamegraph
    /// shows the work an endpoint caused as its descendants and its inclusive time
    /// covers the whole request instead of only the part before the first await.
    ///
    /// Applications instrumented with OpenTelemetry get this without writing any of the
    /// above: <c>Pyroscope.OpenTelemetry.PyroscopeSpanProcessor</c> opens a scope per
    /// span, named after the span.
    ///
    /// Set <c>PYROSCOPE_ASYNC_STITCHING_ENABLED=false</c> to turn stitching off; pushing
    /// a scope then costs nothing and the profiler reports physical stacks only.
    /// </summary>
    public readonly struct AsyncScope : IDisposable
    {
        private readonly ProfilingContext? _context;
        private readonly AsyncScopeNode? _pushed;
        private readonly AsyncScopeNode? _previous;

        internal AsyncScope(ProfilingContext context, AsyncScopeNode? pushed, AsyncScopeNode? previous)
        {
            _context = context;
            _pushed = pushed;
            _previous = previous;
        }

        /// <summary>
        /// Name of the innermost scope the calling async flow is inside, or null.
        /// </summary>
        public static string? Current => Profiler.Instance.ProfilingContext.CurrentScope?.Name;

        /// <summary>
        /// Enters a scope named <paramref name="name"/>, nested inside any scope the
        /// calling flow is already in. Dispose the result to leave it.
        ///
        /// Prefer names with bounded cardinality -- a route template rather than a URL
        /// with ids in it. Every distinct name is retained for the process lifetime, and
        /// past an internal cap new names stop being recorded.
        /// </summary>
        public static AsyncScope Push(string name)
        {
            var context = Profiler.Instance.ProfilingContext;
            var (pushed, previous) = context.PushScope(name);
            return new AsyncScope(context, pushed, previous);
        }

        /// <summary>
        /// Leaves the scope. Safe to call from a different async flow than the one that
        /// pushed it (nothing happens in that case) and safe to call more than once.
        /// </summary>
        public void Dispose()
        {
            _context?.RestoreScope(_pushed, _previous);
        }
    }
}
