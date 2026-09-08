# musllinux_1_2 (Alpine 3.22, musl 1.2). Published per-arch rather than as a
# multi-arch manifest, so each arch has its own digest and gets its own pinned
# stage; BASE_ARCH picks one and the Makefile passes it. Under BuildKit only the
# selected stage is pulled. Bump the tag and both digests together.
ARG BASE_ARCH=x86_64
FROM quay.io/pypa/musllinux_1_2_x86_64:2026.09.05-1@sha256:621f8004ed526a5a6bf6a866fb415ad8da54d59a991e50b3b69167c3a768a616 AS base-x86_64
FROM quay.io/pypa/musllinux_1_2_aarch64:2026.09.05-1@sha256:4dffcd49f0b6fc6928a49915f3cd939f973bbecbdfe96e1e7926b6049bc0bad5 AS base-aarch64
FROM base-${BASE_ARCH} AS builder

# Same static clang toolchain as the glibc build, so one pinned compiler version
# covers both libc flavours (this image carried clang 20 via apk before).
ARG CLANG_VERSION=20.1.8.0
RUN manylinux-install-clang -v ${CLANG_VERSION}

# musl-dbg is the only build dep the image lacks. cmake, git, bash, make,
# gcc/g++/musl-dev, autoconf/automake/libtool, util-linux-dev, xz-dev,
# linux-headers, perl and curl are all preinstalled.
RUN apk add --no-cache musl-dbg

# Build OpenSSL from source with static libs
ARG OPENSSL_VERSION=3.5.8
RUN curl -fsSLO --retry 10 "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared no-tests --prefix=/usr/local/openssl --openssldir=/etc/ssl && \
    make -j$(nproc) && \
    make install_sw && \
    ln -s /usr/local/openssl/lib64 /usr/local/openssl/lib && \
    cd .. && rm -rf openssl-${OPENSSL_VERSION} openssl-${OPENSSL_VERSION}.tar.gz

FROM builder as build

WORKDIR /profiler
ENV IsAlpine=true

ADD build build
ADD profiler profiler
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

# Allow build type to be passed as build arg, default to Release
ARG CMAKE_BUILD_TYPE=Release
# CMAKE_POLICY_VERSION_MINIMUM: the image ships cmake 4, which hard-errors on
# cmake_minimum_required < 3.5; vendored third_party trees still declare 2.6-3.4
# (e.g. profiler/third_party/CxxUrl).
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
# Run profiler unit tests
RUN cd build-${CMAKE_BUILD_TYPE}/profiler && ctest --output-on-failure -E "WrappedFunctionsTest"
# Run wrapper tests with LD_PRELOAD so wrapped functions resolve to the wrapper library
RUN WRAPPER_SO=$(find /profiler/artifacts/profiler-build/DDProf-Deploy/linux-musl -name "Datadog.Linux.ApiWrapper.x64.so" | head -1) && \
    cd build-${CMAKE_BUILD_TYPE}/profiler && \
    LD_PRELOAD="${WRAPPER_SO}" ctest --output-on-failure -R "WrappedFunctionsTest"

FROM busybox:1.38.0-musl@sha256:8635836765b0c4c43970660219739baa58b0883c2e429e4b8918f7dd1519455c
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux-musl/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux-musl/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

