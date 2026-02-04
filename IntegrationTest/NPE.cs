using System.Runtime.CompilerServices;

namespace Example;

public class NPE
{
    public static int COUNTER = 0;

    public static volatile NPE? instance = null;

    public int num()
    {
        return COUNTER;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void work()
    {
        try
        {
            Interlocked.Add(ref NPE.COUNTER, NPE.instance!.num());
        }
        catch
        {
            Interlocked.Increment(ref NPE.COUNTER);
        }
    }
}
