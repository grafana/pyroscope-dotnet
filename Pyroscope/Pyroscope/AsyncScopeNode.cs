namespace Pyroscope
{
    /// <summary>
    /// One link in the logical async scope chain: the scope's name, its parent, and
    /// the id the native profiler interned the chain under.
    ///
    /// Immutable, so a node can be shared by every async flow that branches off the
    /// scope, and reference equality identifies a scope instance -- which is how
    /// <see cref="AsyncScope.Dispose"/> tells "still inside the scope I pushed" from
    /// "this flow has moved on".
    /// </summary>
    internal sealed class AsyncScopeNode
    {
        public AsyncScopeNode(AsyncScopeNode? parent, string name, uint nativeId)
        {
            Parent = parent;
            Name = name;
            NativeId = nativeId;
            Depth = parent == null ? 1 : parent.Depth + 1;
        }

        public AsyncScopeNode? Parent { get; }

        public string Name { get; }

        /// <summary>
        /// Id of this chain in the profiler's native scope store, or 0 when the
        /// profiler is not running or async stitching is disabled. Resolved once, when
        /// the scope is pushed, so publishing it on a continuation costs a single
        /// integer P/Invoke.
        /// </summary>
        public uint NativeId { get; }

        public int Depth { get; }
    }
}
