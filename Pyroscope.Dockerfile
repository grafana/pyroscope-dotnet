# This base is deliberately pinned to an EOL distro. bullseye's glibc 2.31 is what keeps
# the shipped Pyroscope.Profiler.Native.so at a GLIBC_2.30 floor; a newer base raises that
# floor for everyone running the profiler. bookworm's glibc 2.36, for instance, merged
# pthread/dl into libc and needs GLIBC_2.34, which drops Ubuntu 20.04 and Debian 11.
#
# If you change this tag, you MUST also deal with the apt sources below:
#   - moving to a still-supported base: delete the snapshot.debian.org sources.list block
#     and the Check-Valid-Until line; live mirrors work again. Re-check the glibc floor
#     with `readelf -V` before shipping (see PR #425 for the expected symbol set).
#   - staying on an EOL-pinned base: update all three snapshot timestamps to match the new
#     tag, or apt will resolve against packages this image does not actually contain.
#
# Renovate will not do either for you: renovate.json extends security:only-security-updates,
# so only openssl/openssl and grafana/shared-workflows are auto-bumped in this repo.
FROM debian:bullseye-20260406@sha256:bf53effcacca31b60ce97dabc67578f37e43075d716dc90804d3da3a80d2996c AS builder

# Debian 11 (bullseye) reached end of LTS on 2026-08-31: the bullseye-security
# Release file expired on 2026-09-07 and its pool has been removed from the live
# mirrors, so deb.debian.org can no longer resolve this image's packages.
# snapshot.debian.org is immutable; the 20260406T000000Z timestamps below must stay in
# sync with the base image tag above, so the suites agree with the packages already baked
# into the digest. See the note above the FROM line before changing either.
RUN printf '%s\n' \
      "deb http://snapshot.debian.org/archive/debian/20260406T000000Z bullseye main" \
      "deb http://snapshot.debian.org/archive/debian-security/20260406T000000Z bullseye-security main" \
      "deb http://snapshot.debian.org/archive/debian/20260406T000000Z bullseye-updates main" \
      > /etc/apt/sources.list

# snapshot.debian.org serves historical Release files, whose Valid-Until is by
# definition in the past. Retries cover cold-CI connection resets.
RUN printf '%s\n' \
      'Acquire::Retries "5";' \
      'Acquire::Check-Valid-Until "false";' \
      > /etc/apt/apt.conf.d/80-retries

RUN apt-get update && apt-get -y install cmake make git curl golang libtool wget perl

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

