# ToString microbenchmark

Measures `shared::ToString(const WSTRING&)`
(`shared/src/native-src/string.cpp`), which on non-Windows converts UTF-16 to
UTF-8 via `miniutf::to_utf8` (`shared/src/native-src/miniutf.cpp`).

The timed body mirrors the real profiler call site in
`profiler/src/ProfilerEngine/Datadog.Profiler.Native/AllocationsProvider.cpp`:

```cpp
const WCHAR* typeName;                         // null-terminated .NET type name
shared::ToString(shared::WSTRING(typeName));   // measured
```

so the `WSTRING(const WCHAR*)` construction is included along with the
conversion. Inputs are representative .NET type names.

It compiles `string.cpp` and `miniutf.cpp` directly (no linkage to any
profiler build target). The logger from the real call site is omitted.

## Run

```sh
./build.sh && ./tostring-bench
```

Requires `clang++` with C++20. Output columns: `u16_chars` (input length in
UTF-16 code units), `ns/call`, `ns/char`, and `MB/s` of UTF-16 input consumed.
