# manylinux_2_28 (AlmaLinux 8, glibc 2.28). Debian 11 is EOL. The image is
# published per-arch, so BASE_ARCH selects the repository; the Makefile passes it.
ARG BASE_ARCH=x86_64
ARG BASE_TAG=2026.09.05-1
FROM quay.io/pypa/manylinux_2_28_${BASE_ARCH}:${BASE_TAG} AS builder

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
RUN WRAPPER_SO=$(find /profiler/artifacts/profiler-build -name "Datadog.Linux.ApiWrapper.x64.so" | head -1) && \
    cd build-${CMAKE_BUILD_TYPE}/profiler && \
    LD_PRELOAD="${WRAPPER_SO}" ctest --output-on-failure -R "WrappedFunctionsTest"

FROM busybox:1.38.0-glibc@sha256:3ba030337caebbfc2232b22b1e435eb213b28e5844a34942c74555bf904a265a
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

