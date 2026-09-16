namespace Pyroscope
{
    /// A logical scope that the profiler attributes `await` continuations to.
    ///
    /// Every sample taken inside the scope, on whatever thread, gets the scope's name as a
    /// frame above its physical stack, so the work an endpoint caused nests under the
    /// endpoint instead of under a thread-pool dispatch root. Scopes nest. See the README
    /// for examples.
    ///
    /// This works in conjunction with the PYROSCOPE_ASYNC_PROFILING_ENABLED environment
    /// variable, which is off unless set: until it is on, pushing a scope has no effect.
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

        /// Name of the innermost scope the calling async flow is inside, or null.
        public static string? Current => Profiler.Instance.ProfilingContext.CurrentScope?.Name;

        /// Enters a scope named `name`, nested inside any scope the calling flow is already
        /// in. Dispose the result to leave it.
        ///
        /// Prefer names with bounded cardinality: every distinct name is retained for the
        /// process lifetime, and past an internal cap new names stop being recorded.
        public static AsyncScope Push(string name)
        {
            var context = Profiler.Instance.ProfilingContext;
            var (pushed, previous) = context.PushScope(name);
            return new AsyncScope(context, pushed, previous);
        }

        /// Leaves the scope. Safe to call from a different async flow than the one that
        /// pushed it (nothing happens in that case) and safe to call more than once.
        public void Dispose()
        {
            _context?.RestoreScope(_pushed, _previous);
        }
    }
}
