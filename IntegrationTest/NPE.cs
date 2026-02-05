using System.Runtime.CompilerServices;

namespace Example;

public class NPE
{
    private static int _counter;
    private static volatile NPE? _instance = null;
    
    private readonly int _num = 239;
    
    
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Work()
    {
        try
        {
            Interlocked.Add(ref _counter, _instance!._num);
        }
        catch
        {
            Interlocked.Increment(ref _counter);
        }
    }
}

