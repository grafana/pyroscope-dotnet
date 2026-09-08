# Native library validation

`validate_native_libs.py` checks the shipped `.so` files for two things that are
easy to break without noticing until a customer's container fails to start:

1. **glibc floor.** The highest `GLIBC_x.y` symbol version a library references
   is the oldest glibc it can be loaded on. Building on a newer base image
   silently raises it, so the script asserts it against a maximum. musl builds
   must not reference glibc symbols at all.

2. **Undefined symbol snapshot.** The undefined symbols are compared with a
   checked-in list. Removing symbols is generally safe; adding one means the
   loader now has to find it somewhere, which is how a library ends up failing to
   load on a host that does not provide it.

Both checks are ported from the dd-trace-dotnet Nuke build
(`tracer/build/_build/NativeValidation/NativeValidationHelper.cs` and the
`ValidateNativeProfilerGlibcCompatibility` / `TestNativeWrapper` targets), which
was removed from this fork along with the tracer.

## When it runs

Both Dockerfiles run it in the `build` stage, right after `make`, so it gates every
path that produces a library: `make docker/archive` (which is what
`build_linux_profiler.yml` runs for both arches and both libcs, and what
release-please runs to publish), and `docker build --target test`. A drifted library
fails the image build.

Changing a snapshot invalidates `ADD profiler profiler` and so recompiles the
profiler. That is the cost of keeping the lists next to the script.

## Running it by hand

```bash
# against a local build
./profiler/build/NativeValidation/validate_native_libs.py artifacts/profiler-build/DDProf-Deploy/linux

# against a release tarball
curl -sSL -o pyroscope.tar.gz https://github.com/grafana/pyroscope-dotnet/releases/download/pyroscope-1.4.0/pyroscope.1.4.0-glibc-x86_64.tar.gz
mkdir -p /tmp/pyroscope && tar xzf pyroscope.tar.gz -C /tmp/pyroscope
./profiler/build/NativeValidation/validate_native_libs.py /tmp/pyroscope
```

The libc flavour and architecture are detected from the ELF headers, so the
glibc and musl tarballs for both architectures can be passed in one invocation.
`readelf` is the only requirement besides Python 3; both the binutils and the
llvm build work, and `READELF` overrides which one is used.

To accept an intended change to the symbols, re-run with `--update-snapshots`. A
combination whose snapshot is missing is reported as a warning rather than a failure,
and the list is printed verbatim so it can be committed; the glibc floor is still
checked. A snapshot that exists and does not match is a hard failure.

## Current state

The snapshots and the default limit were generated from Docker builds of all four
combinations at the tip of this branch:

| library  | libc  | requires glibc (x64) | requires glibc (arm64) |
| -------- | ----- | -------------------- | ---------------------- |
| profiler | glibc | 2.30                 | 2.30                   |
| wrapper  | glibc | 2.14                 | 2.17                   |
| profiler | musl  | none                 | none                   |
| wrapper  | musl  | none                 | none                   |

The default limit is therefore `2.30`, which is exactly what the `debian:bullseye`
(glibc 2.31) pin at the top of `Pyroscope.Dockerfile` exists to hold - this check is
what makes that comment enforceable. It is much higher than the `2.17` upstream holds
itself to, because Datadog builds on CentOS 7. The symbols above `2.17` are
`__cxa_thread_atexit_impl` (2.18), `getentropy` (2.25), `exp`/`log`/`pow` (2.29) and
`pthread_cond_clockwait` (2.30). Lowering the floor is a separate piece of work; the
point of the limit here is to catch an accidental *increase*.

## Differences from upstream

- Upstream asserts the glibc version is *equal* to an expected value; this
  asserts it is not *above* a limit, which is the property that actually
  matters and does not need an update every time a symbol goes away.
- Upstream passes the libc flavour and architecture in from Nuke build
  parameters; here they are read from the ELF headers.
- Upstream's `HasValidSnapshot` logs the diff and then returns `true`, so a
  changed snapshot never fails their build. Here it does.
- Upstream additionally checks alpine builds against hard-coded lists of the
  symbols provided by musl 1.2.2 and libgcc 10.3.1 on alpine 3.14
  (`Symbols.cs`). That is not ported - the lists are for a distro version we do
  not build on.
