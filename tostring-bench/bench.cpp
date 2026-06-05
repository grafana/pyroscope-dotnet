// Microbenchmark for shared::ToString(const WSTRING&).
//
// Models the (optimized) call site in
// profiler/src/ProfilerEngine/Datadog.Profiler.Native/AllocationsProvider.cpp:
//
//     const WCHAR* typeName;
//     ... shared::ToString(typeName) ...
//
// where typeName is a null-terminated .NET type name. The intermediate
// shared::WSTRING(typeName) allocation has been removed (it was in the
// baseline); ToString now converts the source buffer to UTF-8 directly via
// miniutf::to_utf8(std::u16string_view).
//
// Built standalone (see build.sh): bench.cpp + string.cpp + miniutf.cpp,
// no linkage to any profiler target. The logger from the real call site is
// omitted; we sink the result instead.

// Included by relative path on purpose: this project header is named
// "string.h", so it must NOT be on the -I search path or it shadows the C
// library <string.h> that libc++ pulls in via <cstring>.
#include "../shared/src/native-src/string.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
// Representative .NET type names, as the profiler sees them (const WCHAR*).
struct Input
{
    const char* label;
    const WCHAR* typeName;
};

const Input kInputs[] = {
    {"short", WStr("System.Int32")},
    {"class", WStr("System.String")},
    {"generic-list", WStr("System.Collections.Generic.List`1[[System.Int32]]")},
    {"generic-dict",
     WStr("System.Collections.Generic.Dictionary`2[[System.String],[System.Collections.Generic.List`1[[System.Int32]]]]")},
    // Non-ASCII to exercise the 2-/3-byte UTF-8 encode paths.
    {"non-ascii", WStr("Acme.ÜberManäger.中文TypeName")},
};

// Sink to keep the compiler from optimizing the call away.
volatile std::size_t g_sink = 0;

double NowNs()
{
    using namespace std::chrono;
    return static_cast<double>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

int main()
{
    constexpr int kIterations = 2'000'000;
    constexpr int kWarmup = 100'000;

    // Correctness sanity check: one known type name should round-trip to ASCII.
    {
        std::string s = shared::ToString(WStr("System.String"));
        std::printf("sanity: ToString(\"System.String\") = \"%s\" (len=%zu)\n\n", s.c_str(), s.size());
    }

    std::printf("%-14s %12s %10s %10s %10s\n", "input", "u16_chars", "ns/call", "ns/char", "MB/s");
    std::printf("%-14s %12s %10s %10s %10s\n", "-----", "---------", "-------", "-------", "----");

    for (const Input& in : kInputs)
    {
        const std::size_t nbChars = shared::WSTRING(in.typeName).size();

        // Warm up (also primes any allocator state).
        for (int i = 0; i < kWarmup; ++i)
        {
            std::string s = shared::ToString(in.typeName);
            g_sink += s.size();
        }

        const double t0 = NowNs();
        for (int i = 0; i < kIterations; ++i)
        {
            std::string s = shared::ToString(in.typeName);
            g_sink += s.size() + static_cast<unsigned char>(s.empty() ? 0 : s[0]);
        }
        const double t1 = NowNs();

        const double nsPerCall = (t1 - t0) / kIterations;
        const double nsPerChar = nbChars ? nsPerCall / static_cast<double>(nbChars) : 0.0;
        // Bytes of UTF-16 input consumed per second.
        const double mbPerSec = nbChars
            ? (static_cast<double>(nbChars) * 2.0) / (nsPerCall * 1e-9) / (1024.0 * 1024.0)
            : 0.0;

        std::printf("%-14s %12zu %10.1f %10.2f %10.0f\n", in.label, nbChars, nsPerCall, nsPerChar, mbPerSec);
    }

    // Touch the sink so it is not dead.
    std::fprintf(stderr, "(sink=%zu)\n", const_cast<std::size_t&>(g_sink));
    return 0;
}
