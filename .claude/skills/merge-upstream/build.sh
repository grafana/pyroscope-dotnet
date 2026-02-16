#!/usr/bin/env bash
# Clean build: remove stale build dir, configure, and build the profiler targets
set -euo pipefail

.claude/skills/merge-upstream/cmake_configure.sh
cmake --build _merge_upstreamm_build \
  --target Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64 \
  -j"$(nproc)" 2>&1
