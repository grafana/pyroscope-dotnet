FROM debian:bullseye-20260406@sha256:bf53effcacca31b60ce97dabc67578f37e43075d716dc90804d3da3a80d2996c AS builder

# deb.debian.org (Fastly) intermittently resets connections on cold CI builds; retry apt fetches.
RUN echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries

# Debian 11 LTS ended 2026-08-31: the bullseye-security .debs were purged from the pool
# while the indices are still served, so apt resolves versions that 404 (retries do not
# help -- a 404 is not retried), and bullseye-security's Release file expired 2026-09-07,
# after which apt-get update itself fails. Pin to a snapshot taken just after the final
# bullseye-security update so the build keeps resolving the exact same packages.
#
# Do not "fix" this by bumping the base image. The linker binds each call to the newest
# symbol version the build host offers, so the base image -- not anything in our source
# -- sets the profiler's runtime glibc requirement. A bookworm base measures GLIBC_2.36,
# which drops Ubuntu 22.04 (2.35), RHEL 9 (2.34) and Amazon Linux 2023 (2.34), with no
# compile error and no failing test. The check-glibc-compat.sh step below enforces it.
#
# Plain HTTP because snapshot.debian.org over HTTPS would need ca-certificates, which
# this image lacks and which cannot be installed before apt works. Integrity still comes
# from the signed Release file and its per-package hashes.
ARG DEBIAN_SNAPSHOT=20260901T000000Z
RUN printf '%s\n' \
      "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye main" \
      "deb http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT} bullseye-security main" \
      "deb http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT} bullseye-updates main" \
      > /etc/apt/sources.list && \
    echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/80-no-valid-until

# binutils for the readelf that check-glibc-compat.sh needs; it is otherwise only
# present incidentally, as a transitive dependency.
RUN apt-get update && apt-get -y install cmake make git curl golang libtool wget perl binutils

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

RUN apt-get -y install lsb-release wget software-properties-common gnupg

RUN wget https://apt.llvm.org/llvm.sh && \
  chmod +x llvm.sh && \
  ./llvm.sh 18

ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/llvm-18/bin/

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

# Gate the runtime glibc requirement, so a future base-image change fails the build
# instead of shipping a binary that will not load on a customer's distro. Each file is
# pinned to its own measured value rather than a shared floor, so neither can drift.
#
# The wrapper's floor is per-arch: glibc's arm64 port was added in 2.17, so no symbol
# there can be versioned older than that, while on x86_64 the wrapper reaches back to
# 2.14. Both values are what the shipped 1.4.0 artifacts measure. The profiler is 2.30
# on both. See build/check-glibc-compat.sh.
RUN OUT=/profiler/artifacts/profiler-build/DDProf-Deploy/linux && \
    case "$(uname -m)" in \
        x86_64)  WRAPPER_FLOOR=2.14 ;; \
        aarch64) WRAPPER_FLOOR=2.17 ;; \
        *) echo "no glibc floor recorded for $(uname -m)" >&2; exit 1 ;; \
    esac && \
    build/check-glibc-compat.sh 2.30 "$OUT/Pyroscope.Profiler.Native.so" && \
    build/check-glibc-compat.sh "$WRAPPER_FLOOR" "$OUT/Datadog.Linux.ApiWrapper.x64.so"

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

