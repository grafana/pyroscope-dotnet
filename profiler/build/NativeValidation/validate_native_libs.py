#!/usr/bin/env python3
"""Validate the native profiler libraries against the libc they were built for.

Two checks, both ported from the dd-trace-dotnet Nuke build
(tracer/build/_build/NativeValidation/NativeValidationHelper.cs, removed from
this fork together with the tracer):

  1. glibc floor - the highest GLIBC_x.y symbol version referenced by a library
     is the oldest glibc it can be loaded on. Building on a newer distro
     silently raises it, so it is asserted against a maximum. musl builds must
     not reference glibc symbols at all.

  2. undefined symbol snapshot - the list of undefined symbols is compared with
     a checked-in snapshot. Removing symbols is generally safe, adding them can
     make the library fail to load at runtime on a host that does not provide
     them.

Nothing runs this automatically yet; it is meant to be pointed at build output
or at the .so files from a release tarball:

  ./validate_native_libs.py profiler/_build/DDProf-Deploy/linux
  ./validate_native_libs.py /tmp/pyroscope.1.4.0-glibc-x86_64
"""

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

# The glibc build container is debian:bullseye (glibc 2.31), so this is what its
# output currently needs. Raising it drops support for older distros - do not
# bump it to silence a failure without deciding that first.
DEFAULT_MAX_GLIBC = "2.30"

LIBRARY_GLOBS = ("*Profiler.Native.so", "*Linux.ApiWrapper.x64.so")

GLIBC_VERSION_RE = re.compile(r"@+GLIBC_([0-9][0-9.]*)")


class ValidationError(Exception):
    """A library failed one of the checks."""


def find_readelf() -> str:
    override = os.environ.get("READELF")
    if override:
        return override
    for candidate in ("readelf", "llvm-readelf"):
        found = shutil.which(candidate)
        if found:
            return found
    sys.exit("error: readelf is required (binutils or llvm)")


def parse_version(version: str) -> tuple:
    """2.2.5 -> (2, 2, 5), so that 2.2.5 < 2.14 < 2.30 as glibc numbers them."""
    return tuple(int(part) for part in version.rstrip(".").split("."))


class Library:
    def __init__(self, path: Path, readelf: str):
        self.path = path
        self._readelf = readelf
        self._symbols = None

    def _run(self, *args: str) -> str:
        result = subprocess.run(
            [self._readelf, *args, "-W", str(self.path)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise ValidationError(
                f"readelf failed, is this an ELF file? {result.stderr.strip()}"
            )
        return result.stdout

    @property
    def kind(self) -> str:
        name = self.path.name
        # The wrapper is deployed as Datadog.* and renamed to Pyroscope.* when
        # the release tarball is packed, so accept both names.
        if name.endswith("Linux.ApiWrapper.x64.so"):
            return "wrapper"
        if name.endswith("Profiler.Native.so"):
            return "profiler"
        raise ValidationError("unrecognised library name")

    @property
    def arch(self) -> str:
        machine = ""
        for line in self._run("-h").splitlines():
            if line.strip().startswith("Machine:"):
                machine = line.split(":", 1)[1].strip()
                break
        if "X86-64" in machine:
            return "x64"
        if "AArch64" in machine:
            return "arm64"
        raise ValidationError(f"unsupported architecture: {machine or 'unknown'}")

    @property
    def libc(self) -> str:
        for line in self._run("-d").splitlines():
            if "(NEEDED)" in line and "libc.musl-" in line:
                return "musl"
        return "glibc"

    def dynamic_symbols(self) -> list:
        """(bind, ndx, name) for every .dynsym entry."""
        if self._symbols is not None:
            return self._symbols
        symbols = []
        for line in self._run("--dyn-syms").splitlines():
            fields = [f for f in line.split() if not f.startswith("[")]
            # Num: Value Size Type Bind Vis Ndx Name, where Name may be absent
            # and st_other annotations such as [VARIANT_PCS] are dropped above.
            if len(fields) < 8 or not fields[0][:-1].isdigit():
                continue
            symbols.append((fields[4], fields[6], fields[7]))
        self._symbols = symbols
        return symbols


def glibc_references(symbols: list) -> dict:
    """Every symbol referencing a versioned glibc entry, mapped to its version.

    Weak and defined symbols count too: they are all resolved by the loader
    against whichever glibc is on the host.
    """
    references = {}
    for _bind, _ndx, name in symbols:
        match = GLIBC_VERSION_RE.search(name)
        if match:
            references[name] = match.group(1)
    return references


def undefined_symbols(symbols: list) -> list:
    """Undefined non-weak symbols - the ones the loader has to satisfy elsewhere.

    The @VERSION suffix is stripped so the snapshots do not depend on the
    toolchain version; check_glibc_floor is what looks at the versions.
    """
    return sorted(
        {
            name.split("@")[0]
            for bind, ndx, name in symbols
            if ndx == "UND" and bind != "WEAK"
        }
    )


def check_glibc_floor(library: Library, libc: str, max_glibc: str, log) -> bool:
    symbols = library.dynamic_symbols()
    references = glibc_references(symbols)

    if libc == "musl":
        if references:
            highest = max(references.values(), key=parse_version)
            log(f"FAIL: musl build references glibc symbols (up to GLIBC_{highest}):")
            for name in sorted(references):
                log(f"    {name}")
            return False
        log("  glibc symbols: none (as expected for musl)")
        return True

    if not references:
        log("FAIL: glibc build references no glibc symbols at all - is this the right file?")
        return False

    highest = max(references.values(), key=parse_version)
    log(f"  requires glibc >= {highest} (max allowed {max_glibc})")

    if parse_version(highest) <= parse_version(max_glibc):
        return True

    log(
        f"FAIL: requires glibc {highest}, which is newer than the allowed "
        f"{max_glibc}. Symbols above the limit:"
    )
    limit = parse_version(max_glibc)
    for name in sorted(references):
        if parse_version(references[name]) > limit:
            log(f"    {name}")
    return False


def check_symbol_snapshot(library: Library, snapshot: Path, update: bool, log) -> bool:
    received = undefined_symbols(library.dynamic_symbols())

    if update:
        snapshot.write_text("".join(f"{symbol}\n" for symbol in received))
        log(f"  wrote {snapshot.name}")
        return True

    if not snapshot.exists():
        # Not a failure: we cannot build every arch everywhere, so a combination
        # whose list has never been captured still gets the glibc floor check
        # rather than breaking the build. Print the list so it can be committed.
        log(f"  WARNING: no snapshot at {snapshot.name}, symbols not checked")
        log_received(received, snapshot, log)
        return True

    verified = snapshot.read_text().splitlines()
    if verified == received:
        log(f"  undefined symbols match {snapshot.name}")
        return True

    log(f"FAIL: undefined symbols differ from {snapshot.name}:")
    diff = difflib.unified_diff(
        verified, received, fromfile=str(snapshot), tofile=str(library.path), lineterm=""
    )
    for line in diff:
        log(f"    {line}")
    log("    Removing symbols is generally safe, adding them can break loading on")
    log("    hosts that do not provide them. If the change is intended, re-run with")
    log("    --update-snapshots, or copy the list below into the snapshot file.")
    log_received(received, snapshot, log)
    return False


def log_received(received: list, snapshot: Path, log) -> None:
    """Print the list verbatim, so a CI failure can be resolved by copy-paste."""
    log(f"    --- 8< --- {snapshot.name} --- 8< ---")
    for symbol in received:
        log(f"    {symbol}")
    log("    --- 8< ---")


def check_library(path: Path, readelf: str, max_glibc: str, update: bool) -> bool:
    print(f"== {path}")

    def log(message: str) -> None:
        print(f"  {message}" if message.startswith("FAIL:") else message)

    library = Library(path, readelf)
    try:
        kind = library.kind
        arch = library.arch
        libc = library.libc
    except ValidationError as error:
        print(f"  FAIL: {error}")
        return False

    print(f"  {kind}, {libc}, {arch}")

    try:
        ok = check_glibc_floor(library, libc, max_glibc, log)
        snapshot = SCRIPT_DIR / f"native-{kind}-symbols-{libc}-{arch}.verified.txt"
        ok = check_symbol_snapshot(library, snapshot, update, log) and ok
    except ValidationError as error:
        print(f"  FAIL: {error}")
        return False

    return ok


def collect_libraries(paths: list) -> list:
    libraries = []
    for raw in paths:
        path = Path(raw)
        if path.is_file():
            libraries.append(path)
        elif path.is_dir():
            found = []
            for pattern in LIBRARY_GLOBS:
                found.extend(path.rglob(pattern))
            libraries.extend(sorted(found))
        else:
            sys.exit(f"error: no such file or directory: {path}")
    return libraries


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "paths",
        metavar="PATH",
        nargs="+",
        help="a .so file, or a directory that is searched for the profiler libraries",
    )
    parser.add_argument(
        "--max-glibc",
        default=DEFAULT_MAX_GLIBC,
        metavar="VERSION",
        help=f"highest allowed GLIBC_x.y symbol version (default {DEFAULT_MAX_GLIBC})",
    )
    parser.add_argument(
        "--update-snapshots",
        action="store_true",
        help="rewrite the .verified.txt snapshots instead of comparing",
    )
    args = parser.parse_args()

    readelf = find_readelf()

    libraries = collect_libraries(args.paths)
    if not libraries:
        sys.exit(f"error: no profiler libraries found in: {' '.join(args.paths)}")

    passed = True
    for path in libraries:
        passed &= check_library(path, readelf, args.max_glibc, args.update_snapshots)

    if not passed:
        print("native validation failed")
        return 1

    print("native validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
