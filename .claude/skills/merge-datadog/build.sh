#!/usr/bin/env bash
# Build the profiler targets (assumes cmake has already been configured)
set -euxo pipefail

cmake --build _merge_datadog_build \
  --target Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64 \
  -j"$(nproc)" 2>&1
