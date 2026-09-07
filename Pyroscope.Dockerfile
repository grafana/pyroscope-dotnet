# Build on CentOS 7 (via pypa's manylinux2014) so the shipped .so keeps a glibc 2.17
# baseline and runs on any distro with glibc >= 2.17 -- RHEL 7+, Ubuntu 18.04+,
# Amazon Linux 2/2023, Debian 10+. This matches the floor DataDog enforces for their
# .NET profiler. Building on a newer glibc silently raises the floor and breaks users:
# a bookworm (2.36) base pushes it to GLIBC_2.36, dropping Ubuntu 22.04 and RHEL 9.
# The check-glibc-compat.sh step further down enforces the floor.
#
# cmake and curl come from the manylinux image itself, so they are not installed below.
# manylinux2014 has no multi-arch manifest -- the arch is part of the image name -- so the
# Makefile overrides this per target arch. The default keeps plain `docker build` working.
ARG BASE_IMAGE=quay.io/pypa/manylinux2014_x86_64@sha256:493d2032114d757aaa761a9385ad8497f391503bf71acef9abeeb66682ca5d90
FROM ${BASE_IMAGE} AS builder

# manylinux2014's stock repos already point at vault.centos.org, which is where CentOS 7
# went when it went EOL -- the same mirror DataDog's centos7 build image uses.
# perl-core rather than the minimal `perl`: OpenSSL's Configure needs IPC::Cmd and
# Time::Piece.
RUN yum install -y make git libtool wget perl-core zlib-devel xz && yum clean all

# CentOS 7 predates every official clang 18 binary (they all need glibc >= 2.27), so pull
# clang from conda-forge, which is built against a glibc 2.17 baseline.
#
# conda-forge's clang ships its own gcc 16 libstdc++ headers, which clang 18 cannot parse
# (they use gcc-14+ builtins such as __builtin_popcountg). Delete them and point clang at
# the image's devtoolset-10 libstdc++ instead. The flag goes in a clang config file rather
# than CFLAGS so that nested build systems -- libunwind's autotools, protobuf's cmake --
# pick it up too, and specifically into conda's own <triple>.cfg because that one is
# loaded last and would otherwise win.
#
# --sysroot=/ is needed for the same reason: conda's clang defaults to its own sysroot,
# which has no zlib.h or other CentOS headers. CentOS 7's glibc is 2.17 itself, so
# building against the image root keeps the same floor.
ARG LLVM_VERSION=18
ARG GCC_TOOLCHAIN=/opt/rh/devtoolset-10/root/usr
RUN case "$(uname -m)" in \
      x86_64)  MAMBA_ARCH=linux-64 ;; \
      aarch64) MAMBA_ARCH=linux-aarch64 ;; \
      *) echo "unsupported arch $(uname -m)" >&2; exit 1 ;; \
    esac && \
    curl -sSL "https://micro.mamba.pm/api/micromamba/${MAMBA_ARCH}/latest" -o /tmp/micromamba.tar.bz2 && \
    tar xjf /tmp/micromamba.tar.bz2 -C /tmp bin/micromamba && \
    /tmp/bin/micromamba create -y -p /opt/llvm -c conda-forge \
        "clang=${LLVM_VERSION}" "clangxx=${LLVM_VERSION}" "lld=${LLVM_VERSION}" && \
    rm -rf /opt/llvm/lib/gcc /opt/llvm/include/c++ /opt/llvm/pkgs \
           /tmp/micromamba.tar.bz2 /tmp/bin && \
    for cfg in /opt/llvm/bin/*-conda-linux-gnu.cfg; do \
        printf '%s\n' "--gcc-toolchain=${GCC_TOOLCHAIN}" "--sysroot=/" > "${cfg}"; \
    done

ENV PATH=/opt/llvm/bin:$PATH

# Fail early and loudly if the config file above stops being picked up, rather than
# surfacing later as thousands of libstdc++ template errors.
RUN printf '#include <atomic>\n#include <string>\n#include <zlib.h>\nint main(){return std::string(zlibVersion()).empty();}\n' > /tmp/probe.cpp && \
    clang++ -std=c++20 -stdlib=libstdc++ /tmp/probe.cpp -lz -o /tmp/probe && /tmp/probe && rm -f /tmp/probe /tmp/probe.cpp

# Build OpenSSL from source with static libs
ARG OPENSSL_VERSION=3.5.8
RUN wget -q "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared no-tests --prefix=/usr/local/openssl --openssldir=/etc/ssl && \
    make -j$(nproc) && \
    make install_sw && \
    ln -s /usr/local/openssl/lib64 /usr/local/openssl/lib && \
    cd .. && rm -rf openssl-${OPENSSL_VERSION} openssl-${OPENSSL_VERSION}.tar.gz

FROM builder as build

WORKDIR /profiler

ADD build build
ADD profiler profiler
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

# Allow build type to be passed as build arg, default to Release
ARG CMAKE_BUILD_TYPE=Release

RUN mkdir build-${CMAKE_BUILD_TYPE} && \
    cd build-${CMAKE_BUILD_TYPE} && \
    cmake .. \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
        -DCMAKE_C_FLAGS_DEBUG="-g -O0" \
        -DOPENSSL_ROOT_DIR=/usr/local/openssl

RUN cd build-${CMAKE_BUILD_TYPE} && make -j16 Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64

# Gate the runtime glibc requirement. See build/check-glibc-compat.sh for why this
# cannot be caught by compiling or by the unit tests.
ARG MAX_GLIBC_VERSION=2.17
RUN build/check-glibc-compat.sh "${MAX_GLIBC_VERSION}" \
        artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so \
        artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so

FROM build AS test
RUN cd build-${CMAKE_BUILD_TYPE} && make -j$(nproc) profiler-native-tests wrapper-native-tests
# Run profiler unit tests
RUN cd build-${CMAKE_BUILD_TYPE}/profiler && ctest --output-on-failure -E "WrappedFunctionsTest"
# Run wrapper tests with LD_PRELOAD so wrapped functions resolve to the wrapper library
RUN WRAPPER_SO=$(find /profiler/artifacts/profiler-build -name "Datadog.Linux.ApiWrapper.x64.so" | head -1) && \
    cd build-${CMAKE_BUILD_TYPE}/profiler && \
    LD_PRELOAD="${WRAPPER_SO}" ctest --output-on-failure -R "WrappedFunctionsTest"

FROM busybox:1.38.0-glibc@sha256:3ba030337caebbfc2232b22b1e435eb213b28e5844a34942c74555bf904a265a
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

