# Upstream profiler integration tests

This directory is a curated port of
`DataDog/dd-trace-dotnet@v3.50.0:profiler/test/Datadog.Profiler.IntegrationTests`.
It runs against the profiler-only Pyroscope fork on Linux x64 with .NET 10.

The harness loads `Pyroscope.Profiler.Native.so` directly and captures
Pyroscope Push API requests. It does not restore the removed Datadog tracer,
native loader, Nuke build, or monitoring-home layout.

The following upstream areas are intentionally excluded:

- tracer correlation and code-hotspot tests;
- SSI, Datadog telemetry, and Datadog request metadata tests;
- Windows named-pipe and ETW tests;
- heap-snapshot and reference-chain tests, because heap snapshots are disabled
  in this fork;
- assertions for Datadog's combined profile, high-cardinality thread metadata,
  multipart metrics, and legacy line/timestamp encodings. Equivalent retained
  tests assert Pyroscope's per-profile-type pprof data instead.

The existing Go integration suite remains responsible for end-to-end uploads to
a real Pyroscope server, TLS, OpenTelemetry activation, and the supported .NET
runtime matrix.
