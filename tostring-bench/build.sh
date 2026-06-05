#!/usr/bin/env bash
# Build the ToString microbenchmark by compiling the profiler sources directly
# (no linkage to any existing build target). See bench.cpp for what it measures.
set -euo pipefail

cd "$(dirname "$0")"
REPO=".."
CL="$REPO/shared/src/native-lib/coreclr/src"

case "$(uname -m)" in
    arm64 | aarch64) ARCHDEF="ARM64" ;;
    x86_64 | amd64)  ARCHDEF="AMD64" ;;
    *) echo "unknown arch $(uname -m)" >&2; exit 1 ;;
esac

PLATDEF="-DPLATFORM_UNIX"
if [ "$(uname -s)" = "Linux" ]; then
    PLATDEF="$PLATDEF -DLINUX"
fi

set -x
clang++ -std=c++20 -O2 -fms-extensions \
    -DPAL_STDCPP_COMPAT $PLATDEF -DUNICODE -DHOST_64BIT -DBIT64 \
    "-D$ARCHDEF" -D_GLIBCXX_USE_CXX11_ABI=0 \
    -I "$CL/inc" \
    -I "$CL/pal/inc" \
    -I "$CL/pal/prebuilt/inc" \
    -I "$CL/pal/inc/rt" \
    bench.cpp \
    "$REPO/shared/src/native-src/string.cpp" \
    "$REPO/shared/src/native-src/miniutf.cpp" \
    -o tostring-bench
