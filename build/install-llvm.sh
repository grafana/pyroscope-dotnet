#!/bin/sh
set -eu

LLVM_VERSION="${1:-22}"
LINK_ASAN_RUNTIME="${LINK_ASAN_RUNTIME:-OFF}"
ASAN_RUNTIME_DIR="${ASAN_RUNTIME_DIR:-/opt/llvm-asan}"

asan_runtime_arch() {
    case "$(uname -m)" in
        x86_64) echo "x86_64" ;;
        aarch64|arm64) echo "aarch64" ;;
        *)
            echo "unsupported architecture for ASAN runtime: $(uname -m)" >&2
            exit 1
            ;;
    esac
}

prefer_versioned_clang() {
    if command -v "clang-${LLVM_VERSION}" >/dev/null 2>&1; then
        ln -sf "$(command -v "clang-${LLVM_VERSION}")" /usr/local/bin/clang
    fi
    if command -v "clang++-${LLVM_VERSION}" >/dev/null 2>&1; then
        ln -sf "$(command -v "clang++-${LLVM_VERSION}")" /usr/local/bin/clang++
    fi
}

link_asan_runtime() {
    prefer_versioned_clang

    compiler=""
    if command -v "clang-${LLVM_VERSION}" >/dev/null 2>&1; then
        compiler="clang-${LLVM_VERSION}"
    elif command -v clang >/dev/null 2>&1; then
        compiler="clang"
    else
        echo "clang was not installed" >&2
        exit 1
    fi

    asan_arch="$(asan_runtime_arch)"
    runtime="$("${compiler}" -print-file-name=libclang_rt.asan-${asan_arch}.so)"
    if [ ! -f "${runtime}" ]; then
        runtime="$(find /usr /opt -name "libclang_rt.asan-${asan_arch}.so" -type f 2>/dev/null | sort | tail -n 1 || true)"
    fi
    if [ -z "${runtime}" ] || [ ! -f "${runtime}" ]; then
        echo "libclang_rt.asan-${asan_arch}.so was not found after installing LLVM" >&2
        exit 1
    fi

    mkdir -p "${ASAN_RUNTIME_DIR}"
    ln -sf "${runtime}" "${ASAN_RUNTIME_DIR}/libasan.so"
}

install_debian_llvm() {
    apt-get update
    apt-get install -y --no-install-recommends ca-certificates wget gnupg lsb-release software-properties-common
    wget -q https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh
    chmod +x /tmp/llvm.sh
    /tmp/llvm.sh "${LLVM_VERSION}"
    apt-get install -y --no-install-recommends "clang-${LLVM_VERSION}" "libclang-rt-${LLVM_VERSION}-dev"
    rm -rf /var/lib/apt/lists/* /tmp/llvm.sh
}

install_alpine_llvm() {
    apk add --no-cache bash ca-certificates wget
    wget -q https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh
    chmod +x /tmp/llvm.sh

    # apt.llvm.org's script is Debian/Ubuntu-oriented. Try it first so Alpine
    # picks up support automatically if the script grows it, then fall back.
    if bash /tmp/llvm.sh "${LLVM_VERSION}"; then
        :
    else
        apk add --no-cache clang llvm compiler-rt
    fi

    rm -f /tmp/llvm.sh
}

if command -v apt-get >/dev/null 2>&1; then
    install_debian_llvm
elif command -v apk >/dev/null 2>&1; then
    install_alpine_llvm
else
    echo "unsupported package manager: expected apt-get or apk" >&2
    exit 1
fi

case "${LINK_ASAN_RUNTIME}" in
    ON|on|1|true|TRUE|yes|YES)
        link_asan_runtime
        ;;
esac
