namespace Pyroscope
{
    /// The LabelSet that is current for a piece of work, including any work it does after an
    /// `await`. Dispose it to restore the previous set.
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

        /// Restores the label set that was current before this scope. Safe to call from a
        /// different async flow than the one that pushed it (nothing happens in that case)
        /// and safe to call more than once.
        public void Dispose()
        {
            _context?.RestoreLabels(_pushed, _previous);
        }
    }
}
