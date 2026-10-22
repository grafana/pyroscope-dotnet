# manylinux_2_28 (AlmaLinux 8, glibc 2.28). Debian 11 is EOL. The image is
# published per-arch rather than as a multi-arch manifest, so each arch has its
# own digest and gets its own pinned stage; BASE_ARCH picks one and the Makefile
# passes it. Under BuildKit only the selected stage is pulled. Bump the tag and
# both digests together.
ARG BASE_ARCH=x86_64
FROM quay.io/pypa/manylinux_2_28_x86_64:2026.09.05-1@sha256:53390351aeb4688114b02c36a23b3e6ce1166ee9b7afc5df1a4f776354fc764c AS base-x86_64
FROM quay.io/pypa/manylinux_2_28_aarch64:2026.09.05-1@sha256:ad74e53b713f3b07d8c889c526dc0c6500da9827b45e38739570875fef52e28f AS base-aarch64
FROM base-${BASE_ARCH} AS builder

# clang via the image's own helper: installs a sha256-verified static toolchain
# into /opt/clang (already first on PATH) and writes clang.cfg with
# --gcc-toolchain=/opt/rh/gcc-toolset-14/root/usr, so clang uses GCC 14's
# libstdc++ -- the profiler needs <span>, which AlmaLinux 8's system libstdc++
# (GCC 8) lacks. AlmaLinux's own llvm-toolset caps at clang 17, and there is no
# RPM equivalent of apt.llvm.org, which is why this replaces the llvm.sh install.
ARG CLANG_VERSION=20.1.8.0
RUN manylinux-install-clang -v ${CLANG_VERSION}

# OpenSSL 3's Configure/Makefile.in need perl modules that el8 splits into
# separate RPMs (IPC::Cmd, Time::Piece, ...); perl-core pulls the lot. Everything
# else the build needs (cmake, make, git, curl, autoconf/automake/libtool,
# gcc-toolset-14) already ships in the image.
RUN dnf -y install perl-core && dnf clean all

# Build OpenSSL from source with static libs
ARG OPENSSL_VERSION=3.5.9
RUN curl -fsSLO --retry 10 "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared no-tests --prefix=/usr/local/openssl --openssldir=/etc/ssl && \
    make -j$(nproc) && \
    make install_sw && \
    ln -s /usr/local/openssl/lib64 /usr/local/openssl/lib && \
    cd .. && rm -rf openssl-${OPENSSL_VERSION} openssl-${OPENSSL_VERSION}.tar.gz

# Rust for the pyroscope-uploader static library. rustup-init is pinned by
# version and sha256 per arch; the toolchain comes from rust-toolchain.toml.
ARG RUSTUP_VERSION=1.29.2
ENV RUSTUP_HOME=/usr/local/rustup CARGO_HOME=/usr/local/cargo PATH=/usr/local/cargo/bin:$PATH
RUN case "$(uname -m)" in \
        x86_64) sha=5be191e8c70a5007074254cb74d78dbfdd25cd5a61c27de7b8e5410122ec24f4 ;; \
        aarch64) sha=a9c168dbea81a84671c6b7a5c1795cb0eb699020110d107ddfca427bd797e697 ;; \
        *) echo "unsupported arch $(uname -m)"; exit 1 ;; \
    esac && \
    curl -fsSLo rustup-init --retry 10 "https://static.rust-lang.org/rustup/archive/${RUSTUP_VERSION}/$(uname -m)-unknown-linux-gnu/rustup-init" && \
    echo "${sha}  rustup-init" | sha256sum -c - && \
    chmod +x rustup-init && \
    ./rustup-init -y --no-modify-path --profile minimal --default-toolchain none && \
    rm rustup-init
COPY profiler/src/ProfilerEngine/pyroscope-uploader/rust-toolchain.toml /tmp/rust-toolchain/rust-toolchain.toml
RUN cd /tmp/rust-toolchain && rustup toolchain install && rm -rf /tmp/rust-toolchain

FROM builder as build

WORKDIR /profiler

ADD build build
ADD profiler profiler
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

# Allow build type to be passed as build arg, default to Release
ARG CMAKE_BUILD_TYPE=Release

# CMAKE_POLICY_VERSION_MINIMUM: the image ships cmake 4, which hard-errors on
# cmake_minimum_required < 3.5; vendored third_party trees may still declare
# older versions.
RUN mkdir build-${CMAKE_BUILD_TYPE} && \
    cd build-${CMAKE_BUILD_TYPE} && \
    cmake .. \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
        -DCMAKE_C_FLAGS_DEBUG="-g -O0" \
        -DOPENSSL_ROOT_DIR=/usr/local/openssl

RUN cd build-${CMAKE_BUILD_TYPE} && make -j16 Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64

FROM build AS test
RUN cd build-${CMAKE_BUILD_TYPE} && make -j$(nproc) profiler-native-tests wrapper-native-tests
# Run pyroscope-uploader unit tests
RUN cd profiler/src/ProfilerEngine/pyroscope-uploader && \
    OPENSSL_DIR=/usr/local/openssl OPENSSL_STATIC=1 cargo test --locked --target-dir /profiler/build-${CMAKE_BUILD_TYPE}/pyroscope-uploader-test
# Run profiler unit tests
RUN cd build-${CMAKE_BUILD_TYPE}/profiler && ctest --output-on-failure -E "WrappedFunctionsTest"
# Run wrapper tests with LD_PRELOAD so wrapped functions resolve to the wrapper library
RUN WRAPPER_SO=$(find /profiler/artifacts/profiler-build -name "Datadog.Linux.ApiWrapper.x64.so" | head -1) && \
    cd build-${CMAKE_BUILD_TYPE}/profiler && \
    LD_PRELOAD="${WRAPPER_SO}" ctest --output-on-failure -R "WrappedFunctionsTest"

FROM busybox:1.38.0-glibc@sha256:99813cb178ace2cd9c6c53ee3bddef5d2f5708aa438335111d7a9e3c5e1aaebe
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

