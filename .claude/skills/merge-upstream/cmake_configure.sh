#!/usr/bin/env bash
# Run cmake configure (does not clean the build directory)
set -euxo pipefail

CXX=clang++ CC=clang cmake -G "Unix Makefiles" -S . -B _merge_upstreamm_build
