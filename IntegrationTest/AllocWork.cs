using System.Runtime.CompilerServices;

namespace Example;

public class AllocWork
{
    private static object? _sink;

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Work()
    {
        for (int i = 0; i < 64; i++)
        {
            _sink = Allocate();
        }
        GC.KeepAlive(_sink);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static AllocatedLeafPayload Allocate()
    {
        return new AllocatedLeafPayload(i: 42);
    }
}

public class AllocatedLeafPayload
{
    public PayloadBuffer Buffer;

    public AllocatedLeafPayload(long i)
    {
        Buffer[0] = new PayloadChunk(i);
    }
}

[InlineArray(2048)]
public struct PayloadBuffer
{
    private PayloadChunk _element0;
}

public readonly struct PayloadChunk
{
    public readonly long Value01;
    public readonly long Value02;
    public readonly long Value03;
    public readonly long Value04;
    public readonly long Value05;
    public readonly long Value06;
    public readonly long Value07;
    public readonly long Value08;

    public PayloadChunk(long value)
    {
        Value01 = value;
        Value02 = value;
        Value03 = value;
        Value04 = value;
        Value05 = value;
        Value06 = value;
        Value07 = value;
        Value08 = value;
    }
}
