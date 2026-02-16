#!/usr/bin/env bash
# Clean build: remove stale build dir, configure, and build the profiler targets
set -euo pipefail

rm -rf _merge_upstreamm_build
CXX=clang++ CC=clang cmake -G "Unix Makefiles" -S . -B _merge_upstreamm_build
cmake --build _merge_upstreamm_build \
  --target Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64 \
  -j"$(nproc)" 2>&1
