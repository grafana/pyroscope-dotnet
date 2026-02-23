# pyroscope-dotnet

Fork of [dd-trace-dotnet](https://github.com/DataDog/dd-trace-dotnet). The upstream tracer has been removed — only the **profiler** remains. This repo builds and ships the Pyroscope .NET profiler (`Pyroscope.Profiler.Native.so` and `Pyroscope.Linux.ApiWrapper.x64.so`).

## Build the profiler (Debug, glibc/Debian)

```bash
docker build -f Pyroscope.Dockerfile \
  --build-arg CMAKE_BUILD_TYPE=Debug \
  -t pyroscope-dotnet-debug .
```

Output artifacts inside the image:
- `/Pyroscope.Profiler.Native.so`
- `/Pyroscope.Linux.ApiWrapper.x64.so`

Extract them:
```bash
id=$(docker create pyroscope-dotnet-debug)
docker cp $id:/Pyroscope.Profiler.Native.so .
docker cp $id:/Pyroscope.Linux.ApiWrapper.x64.so .
docker rm $id
```

## Build the profiler (Debug, musl/Alpine)

```bash
docker build -f Pyroscope.musl.Dockerfile \
  --build-arg CMAKE_BUILD_TYPE=Debug \
  -t pyroscope-dotnet-debug-musl .
```

Same extraction steps as above.

## Key files

| Path | Description |
|------|-------------|
| `profiler/` | Native profiler source (C++) |
| `shared/` | Shared native code |
| `build/` | CMake build scripts |
| `Pyroscope/` | Managed (.NET) SDK |
| `IntegrationTest/` | Integration test app (rideshare) |
| `Pyroscope.Dockerfile` | glibc build (Debian 11 + LLVM 18) |
| `Pyroscope.musl.Dockerfile` | musl build (Alpine 3.18) |
| `itest.Dockerfile` | Integration test image |
