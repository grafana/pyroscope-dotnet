using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Pyroscope;

namespace Example;

internal sealed class TieredCompilationWorkload
{
    public record State(string Runtime, string Architecture, ulong InitialAddress, ulong PromotedAddress, int PostCalls);

    private readonly object _lock = new();
    private nuint _initialAddress;
    private nuint _promotedAddress;
    private int _postCalls;
    private static long _sink;

    [DllImport("TieringProbe", EntryPoint = "tiering_instruction_pointer")]
    [SuppressGCTransition]
    private static extern nuint InstructionPointer();

    // One native call site, no inlining, and OSR disabled by the test: a changed
    // return address means this invocation executes a different native body.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static nuint HotLoop(int milliseconds)
    {
        nuint address = InstructionPointer();
        long deadline = Stopwatch.GetTimestamp() + milliseconds * Stopwatch.Frequency / 1000;
        long value = 1;
        do
        {
            for (int i = 0; i < 100_000; i++) value = unchecked(value * 1664525 + 1013904223);
            Volatile.Write(ref _sink, value);
        } while (Stopwatch.GetTimestamp() < deadline);
        return address;
    }

    public State Prepare()
    {
        lock (_lock)
        {
            if (_initialAddress != 0) throw new InvalidOperationException("Tiering workload already prepared");
            LabelsWrapper.Do(LabelSet.Empty.BuildUpon().Add("tiering_phase", "warmup").Build(), () =>
            {
                // Leave enough CPU work in the initial body to establish that
                // profiling works before checking the promoted body separately.
                _initialAddress = HotLoop(1000);
                var timeout = Stopwatch.StartNew();
                do
                {
                    Thread.Sleep(20);
                    _promotedAddress = HotLoop(50);
                    if (_promotedAddress != _initialAddress && _promotedAddress != 0) return;
                } while (timeout.Elapsed < TimeSpan.FromSeconds(30));
                throw new InvalidOperationException($"No tier transition: initial=0x{_initialAddress:x}, current=0x{_promotedAddress:x}");
            });
            return Snapshot();
        }
    }

    public State Run()
    {
        lock (_lock)
        {
            if (_initialAddress == 0 || _initialAddress == _promotedAddress)
                throw new InvalidOperationException("A confirmed tier transition is required before sampling");
            LabelsWrapper.Do(LabelSet.Empty.BuildUpon().Add("tiering_phase", "post-transition").Build(), PostTransition);
            return Snapshot();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining | MethodImplOptions.NoOptimization)]
    private void PostTransition()
    {
        var duration = Stopwatch.StartNew();
        do
        {
            nuint address = HotLoop(50);
            if (address == 0 || address == _initialAddress)
                throw new InvalidOperationException("Post-transition workload executed the initial native body");
            _promotedAddress = address;
            _postCalls++;
        } while (duration.Elapsed < TimeSpan.FromSeconds(5));
    }

    private State Snapshot() => new(RuntimeInformation.FrameworkDescription,
        RuntimeInformation.ProcessArchitecture.ToString(), (ulong)_initialAddress, (ulong)_promotedAddress, _postCalls);
}
