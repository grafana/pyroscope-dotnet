# pyroscope-dotnet

Fork of [dd-trace-dotnet](https://github.com/DataDog/dd-trace-dotnet). The upstream tracer has been removed — only the **profiler** remains. This repo builds and ships the Pyroscope .NET profiler for both Linux (`Pyroscope.Profiler.Native.so` and `Pyroscope.Linux.ApiWrapper.x64.so`) and Windows (`Pyroscope.Profiler.Native.dll`).

## Committing

This repo's ruleset requires verified commit signatures. In Claude Code remote sessions, plain `git commit` + `git push` already produces signed commits (signing is pre-configured in the session) — do **not** use the GitHub MCP file tools (`create_or_update_file`, `delete_file`, `push_files`) to commit; those produce unsigned commits and are rejected with a 409. Details: [docs/signed-commits-from-claude-code.md](docs/signed-commits-from-claude-code.md).

## Setup

The repo uses git submodules for third-party dependencies. Check them out before doing any work:

```bash
git submodule update --init --recursive
```

## Build the profiler (Linux, Debug)

Requires clang/clang++ and cmake. Uses `build-claude-Debug` as the build directory. Always use the Unix Makefiles generator (never Ninja).

```bash
mkdir build-claude-Debug
cd build-claude-Debug
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
    -DCMAKE_C_FLAGS_DEBUG="-g -O0"
make -j$(nproc) Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64
```

On hosts that ship only dynamic OpenSSL (e.g. Fedora's `openssl-devel`), add `-DUSE_STATIC_OPENSSL=OFF` to the `cmake` invocation.

Output artifacts:
- `artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so`
- `artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so`

## Build the profiler (Windows)

Windows is a supported target: CI (`.github/workflows/windows.yml`) builds `Pyroscope.Profiler.Native.dll` (Release x64) with MSBuild and runs the integration tests against it on Windows runners. Local build (requires MSVC + vcpkg):

```powershell
vcpkg integrate install
msbuild profiler\src\ProfilerEngine\Datadog.Profiler.Native.Windows\Datadog.Profiler.Native.Windows.vcxproj `
    /p:Configuration=Release /p:Platform=x64 /p:VcpkgEnableManifest=true /m
```

Output artifact:
- `artifacts/profiler-build/bin/Release-x64/profiler/src/ProfilerEngine/Datadog.Profiler.Native.Windows/Pyroscope.Profiler.Native.dll`

Note: the vcxproj pins toolset v143 + SDK 10.0.19041; CI overrides these via `/p:PlatformToolset` and `/p:WindowsTargetPlatformVersion` to whatever the runner image ships (see the workflow for details). Windows-only sources live in `profiler/src/ProfilerEngine/Datadog.Profiler.Native.Windows/` — they are shipped code, not dead upstream leftovers.