#!/usr/bin/env python3
"""Regenerate the pinned sysroot package lists.

The glibc ABI floor of the released artifacts is set by the Debian 11 sysroot
that Pyroscope.Dockerfile builds from these lists, so the lists are checked in
and pinned by sha256 rather than resolved at build time. Run this only when
deliberately changing the floor or the package set, and review the diff.

Package versions come from archive.debian.org's frozen bullseye 11.11 pool,
which is a mutually consistent snapshot; sha256 sums come from its signed
Packages index.
"""
import gzip
import os
import urllib.request

BASE = "https://archive.debian.org/debian/"
SUITE = "bullseye"
ARCHES = ("amd64", "arm64")

# Kept explicit rather than resolved through apt: we want headers, link stubs
# and static archives, not a runnable chroot.
PACKAGES = [
    # glibc: headers, linker scripts, libc_nonshared.a, and the .so.6 files the
    # linker resolves symbol versions against.
    "libc6", "libc6-dev",
    # libcrypt left glibc in bullseye and libc6-dev depends on it.
    "libcrypt1", "libcrypt-dev",
    # rpc headers pulled in by libc6-dev.
    "libnsl2", "libnsl-dev", "libtirpc3", "libtirpc-dev",
    # kernel uapi headers.
    "linux-libc-dev",
    # libstdc++.a and libgcc.a are statically linked into the profiler, so they
    # must come from here too, not from the base image.
    "libgcc-s1", "libgcc-10-dev", "libstdc++6", "libstdc++-10-dev",
    # Runtimes that libgcc-10-dev symlinks to. Needed so the sysroot has no
    # dangling symlinks, and so RUN_ASAN/RUN_TSAN/RUN_UBSAN builds still link.
    # libquadmath0 is x86-only; missing packages are skipped per arch.
    "libasan6", "libtsan0", "libubsan1", "liblsan0",
    "libitm1", "libgomp1", "libatomic1", "libquadmath0",
]

here = os.path.dirname(os.path.abspath(__file__))

for arch in ARCHES:
    url = f"{BASE}dists/{SUITE}/main/binary-{arch}/Packages.gz"
    index = gzip.decompress(urllib.request.urlopen(url, timeout=180).read())
    found = {}
    for para in index.decode("utf-8", "replace").split("\n\n"):
        fields = {}
        for line in para.split("\n"):
            if line[:1] in (" ", "\t"):
                continue
            key, sep, val = line.partition(":")
            if sep:
                fields[key.strip()] = val.strip()
        name = fields.get("Package")
        if name in PACKAGES:
            found.setdefault(name, fields)

    out = os.path.join(here, f"{SUITE}-{arch}.sha256")
    total = 0
    with open(out, "w") as fh:
        for name in PACKAGES:
            pkg = found.get(name)
            if pkg is None:
                print(f"{arch}: skipping {name} (not in {SUITE}/{arch})")
                continue
            total += int(pkg["Size"])
            fh.write(f'{pkg["SHA256"]}  {pkg["Filename"]}\n')
    print(f"{arch}: wrote {len(found)} packages, {total // 1024 // 1024} MiB -> {out}")
