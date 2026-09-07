#!/bin/sh
# Assert that the released native libraries stay within the glibc ABI floor we
# promise to users.
#
# The floor used to be an accident of whatever glibc the build image shipped.
# It is now set deliberately by the Debian 11 sysroot in Pyroscope.Dockerfile,
# and this script is what keeps it honest: it runs in the `build` stage, so a
# base-image bump that raised the floor fails the build instead of shipping.
#
# MAX_GLIBC is the highest GLIBC_x.y symbol version any artifact may reference.
# It is measured, not guessed -- see the values printed below. Raising it is a
# user-visible compatibility break and should be a deliberate, reviewed change.
set -eu

MAX_GLIBC="${MAX_GLIBC:-2.30}"

# Static linking of libstdc++/libgcc is load-bearing for the floor: a dynamic
# dependency on either would import the build image's much newer ABI.
FORBIDDEN_NEEDED="libstdc++.so.6 libgcc_s.so.1 libssl.so libcrypto.so"

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <lib.so>..." >&2
    exit 2
fi

status=0

# Highest version among the arguments, using version-aware sort.
max_version() {
    printf '%s\n' "$@" | sort -V | tail -1
}

for so in "$@"; do
    if [ ! -f "$so" ]; then
        echo "FAIL $so: not found" >&2
        status=1
        continue
    fi

    echo "== $so"

    versions="$(readelf --dyn-syms --wide "$so" \
        | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' \
        | sed 's/^GLIBC_//' \
        | sort -uV || true)"

    if [ -z "$versions" ]; then
        echo "   glibc refs: none"
    else
        echo "   glibc refs: $(echo "$versions" | tr '\n' ' ')"
        actual_max="$(printf '%s\n' "$versions" | tail -1)"
        echo "   max glibc:  $actual_max (allowed: $MAX_GLIBC)"
        if [ "$(max_version "$actual_max" "$MAX_GLIBC")" != "$MAX_GLIBC" ]; then
            echo "FAIL $so requires GLIBC_$actual_max > GLIBC_$MAX_GLIBC" >&2
            status=1
        fi
    fi

    # A GLIBCXX_/CXXABI_ reference means libstdc++ leaked in dynamically.
    cxx="$(readelf --dyn-syms --wide "$so" \
        | grep -oE '(GLIBCXX|CXXABI)_[0-9.]+' | sort -u || true)"
    if [ -n "$cxx" ]; then
        echo "FAIL $so references C++ runtime versions: $(echo "$cxx" | tr '\n' ' ')" >&2
        status=1
    fi

    needed="$(readelf -d "$so" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p')"
    echo "   needed:     $(echo "$needed" | tr '\n' ' ')"
    for bad in $FORBIDDEN_NEEDED; do
        if echo "$needed" | grep -q "^${bad}"; then
            echo "FAIL $so dynamically links $bad; it must be static" >&2
            status=1
        fi
    done

    # An executable stack makes glibc >= 2.41 refuse to load the library.
    if readelf -lW "$so" | grep -q 'GNU_STACK.*RWE'; then
        echo "FAIL $so requests an executable stack" >&2
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "glibc ABI check passed (floor GLIBC_$MAX_GLIBC)"
fi
exit "$status"
