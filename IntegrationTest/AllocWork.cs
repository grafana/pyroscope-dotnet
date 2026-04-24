using System.Runtime.CompilerServices;

namespace Example;

public class AllocWork
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Work()
    {
        for (int i = 0; i < 1000; i++)
        {
            Allocate();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static byte[] Allocate()
    {
        return new byte[10240];
    }
}
