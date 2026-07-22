#!/usr/bin/env bash
# Run cmake configure (does not clean the build directory)
set -euxo pipefail

# A few build dependencies are pulled over HTTPS from GitHub via CMake's
# FetchContent (e.g. GoogleTest as an archive .zip). In Claude Code web sessions
# outbound HTTPS to github.com is routed through a GitHub-scoped proxy that only
# permits this repository, so those archive downloads return 403. Bypass the
# proxy for GitHub download hosts so FetchContent can fetch them directly.
# (Requires the environment's network access to allow direct egress; on a normal
# machine/CI this is a no-op since traffic already goes direct. Dependencies that
# are git-cloned — Re2, Libunwind — are unaffected: they resolve through git's
# own proxy config, not these variables.)
export no_proxy="${no_proxy:+$no_proxy,}github.com,codeload.github.com,githubusercontent.com,objects.githubusercontent.com,release-assets.githubusercontent.com"
export NO_PROXY="$no_proxy"

CXX=clang++ CC=clang cmake -G "Unix Makefiles" -S . -B _merge_datadog_build
