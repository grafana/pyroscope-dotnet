namespace Pyroscope
{
    /// <summary>
    /// The <see cref="LabelSet"/> that is current for a piece of work, including any work it does
    /// after an <c>await</c>. Dispose it to restore the previous set.
    /// </summary>
    public readonly struct LabelScope : IDisposable
    {
        private readonly ProfilingContext? _context;
        private readonly LabelSet? _pushed;
        private readonly LabelSet? _previous;

        internal LabelScope(ProfilingContext context, LabelSet? pushed, LabelSet? previous)
        {
            _context = context;
            _pushed = pushed;
            _previous = previous;
        }

        /// <summary>
        /// Restores the label set that was current before this scope. Safe to call from a different
        /// async flow than the one that pushed it (nothing happens in that case) and safe to call
        /// more than once.
        /// </summary>
        public void Dispose()
        {
            _context?.RestoreLabels(_pushed, _previous);
        }
    }
}
