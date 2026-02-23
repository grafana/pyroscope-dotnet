# pyroscope-dotnet

Fork of [dd-trace-dotnet](https://github.com/DataDog/dd-trace-dotnet). The upstream tracer has been removed — only the **profiler** remains. This repo builds and ships the Pyroscope .NET profiler (`Pyroscope.Profiler.Native.so` and `Pyroscope.Linux.ApiWrapper.x64.so`).

## Setup

The repo uses git submodules for third-party dependencies. Check them out before doing any work:

```bash
git submodule update --init --recursive
```

## Build the profiler (Debug)

Requires clang/clang++ and cmake. Uses `build-claude-Debug` as the build directory.

```bash
mkdir build-claude-Debug
cd build-claude-Debug
cmake .. \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
    -DCMAKE_C_FLAGS_DEBUG="-g -O0"
make -j$(nproc) Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64
```

Output artifacts:
- `profiler/_build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so`
- `profiler/_build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so`

For musl/Alpine builds, set `IsAlpine=true` and artifacts will be under `profiler/_build/DDProf-Deploy/linux-musl/`.
