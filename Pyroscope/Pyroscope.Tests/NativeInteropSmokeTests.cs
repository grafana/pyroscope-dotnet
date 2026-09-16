using System.Runtime.InteropServices;
using NUnit.Framework;

namespace Pyroscope.Tests;

// Calls the profiler's entry points for real, to check that the P/Invoke declarations bind: the
// propagation tests substitute the sink, so a signature the runtime refuses to marshal would sail
// through them. The calls return early inside the profiler because no CLR profiler is attached to
// the test process; what is under test is the boundary, not the behaviour behind it.
//
// Skipped when the native library has not been built. Point PYROSCOPE_NATIVE_SO at it, or build it
// as described in CLAUDE.md and it is found automatically.
[TestFixture]
public class NativeInteropSmokeTests
{
    [OneTimeSetUp]
    public void ResolveNativeLibrary()
    {
        var path = FindNativeLibrary();
        if (path == null)
        {
            Assert.Ignore("native profiler library not built; set PYROSCOPE_NATIVE_SO to run these");
            return;
        }

        TestContext.Out.WriteLine($"native library: {path}");
        NativeLibrary.SetDllImportResolver(
            typeof(NativeInterop).Assembly,
            (name, _, _) => name == "Pyroscope.Profiler.Native" ? NativeLibrary.Load(path) : IntPtr.Zero);
    }

    [Test]
    public void PushAsyncScopeBinds()
    {
        Assert.DoesNotThrow(() => NativeInterop.PushAsyncScope(0, "GET /folders"));
    }

    [Test]
    public void InternDynamicTagSetBinds()
    {
        Assert.DoesNotThrow(() => NativeInterop.InternDynamicTagSet(
            new[] { "endpoint", "vehicle" },
            new[] { "async-order", "bike" },
            2));
    }

    [Test]
    public void InternDynamicTagSetHandlesNonAsciiValues()
    {
        // The hand-rolled UTF-8 marshalling exists because the runtime's string[] marshalling
        // would go through the platform ANSI code page and mangle these.
        Assert.DoesNotThrow(() => NativeInterop.InternDynamicTagSet(
            new[] { "région" },
            new[] { "eu-nörd" },
            1));
    }

    [Test]
    public void SetCurrentProfilingContextBinds()
    {
        Assert.DoesNotThrow(() => NativeInterop.SetCurrentProfilingContext(0, 0));
    }

    [Test]
    public void SetAndClearDynamicTagBind()
    {
        Assert.DoesNotThrow(() =>
        {
            NativeInterop.SetDynamicTag("endpoint", "async-order");
            NativeInterop.ClearDynamicTags();
        });
    }

    private static string? FindNativeLibrary()
    {
        var fromEnv = Environment.GetEnvironmentVariable("PYROSCOPE_NATIVE_SO");
        if (!string.IsNullOrEmpty(fromEnv))
        {
            return File.Exists(fromEnv) ? fromEnv : null;
        }

        // Walk up to the repository root and look where the build recipe in CLAUDE.md puts it.
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null)
        {
            var candidate = Path.Combine(
                directory.FullName,
                "artifacts", "profiler-build", "DDProf-Deploy", "linux", "Pyroscope.Profiler.Native.so");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            directory = directory.Parent;
        }

        return null;
    }
}
