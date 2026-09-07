#!/usr/bin/env bash
#
# Fail if a shipped shared object requires a glibc newer than our supported floor.
#
# The floor is set by the glibc of the *build* image, not by anything in our source:
# the linker binds each call to the newest symbol version the build host offers. So a
# base-image bump silently raises the runtime requirement and breaks users on older
# distros, with no compile error and no test failure. This check makes that a build
# failure instead.
#
# Reference points for the floor: glibc 2.17 = RHEL 7 / Amazon Linux 2,
# 2.28 = RHEL 8 / Debian 10, 2.31 = Ubuntu 20.04, 2.34 = RHEL 9 / Amazon Linux 2023,
# 2.35 = Ubuntu 22.04, 2.36 = Debian 12.
#
# Note that the floor is per-arch. glibc's arm64 port was added in 2.17, so on aarch64
# every symbol is versioned at 2.17 or newer and a floor below that is unsatisfiable.
#
# Usage: check-glibc-compat.sh <max-glibc-version> <file.so> [file.so ...]

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <max-glibc-version> <file.so> [file.so ...]" >&2
    exit 2
fi

max_version="$1"
shift

# `nm` does not report symbol versions reliably across binutils releases, so read the
# version-requirements section with readelf instead.
required_versions() {
    readelf -V "$1" | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -uV
}

status=0

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "FAIL $file: not found" >&2
        status=1
        continue
    fi

    versions="$(required_versions "$file")"
    if [ -z "$versions" ]; then
        echo "OK   $(basename "$file"): no versioned glibc dependencies"
        continue
    fi

    highest="$(printf '%s\n' "$versions" | tail -1)"

    # sort -V puts the greater of the two last; equal versions are fine.
    if [ "$highest" != "$max_version" ] && \
       [ "$(printf '%s\n%s\n' "$highest" "$max_version" | sort -V | tail -1)" = "$highest" ]; then
        echo "FAIL $(basename "$file"): requires GLIBC_${highest}, above the GLIBC_${max_version} floor" >&2
        echo "     offending symbols:" >&2
        readelf --dyn-syms --wide "$file" \
            | grep -oE '@GLIBC_[0-9]+(\.[0-9]+)+ .*|[A-Za-z_][A-Za-z0-9_]*@+GLIBC_[0-9]+(\.[0-9]+)+' \
            | grep -vE "@\+?GLIBC_(${max_version//./\\.})$" \
            | awk -F'@' -v max="$max_version" '{
                  v = $NF; sub(/^\+/, "", v); sub(/^GLIBC_/, "", v);
                  split(v, a, "."); split(max, b, ".");
                  if (a[1] > b[1] || (a[1] == b[1] && a[2] > b[2])) print "       " $0
              }' \
            | sort -u >&2 || true
        status=1
    else
        echo "OK   $(basename "$file"): max GLIBC_${highest} (floor GLIBC_${max_version})"
    fi
done

exit $status
