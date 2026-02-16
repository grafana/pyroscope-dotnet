#!/usr/bin/env bash
# Clean build directory and run cmake configure
set -euo pipefail

rm -rf _merge_upstreamm_build
CXX=clang++ CC=clang cmake -G "Unix Makefiles" -S . -B _merge_upstreamm_build
