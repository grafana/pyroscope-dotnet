FROM debian:13@sha256:f324c7ff54321e8d9c588493a20244965938ce0aa50bbd1022d38010e9ffc4b1 AS builder

# deb.debian.org (Fastly) intermittently resets connections on cold CI builds; retry apt fetches.
RUN echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries

# Where the bullseye sysroot that defines our glibc ABI floor gets unpacked.
ARG SYSROOT=/sysroot
ENV SYSROOT=${SYSROOT}

# archive.debian.org holds the permanent, frozen bullseye 11.11 pool.
# snapshot.debian.org is an independent fallback carrying the same paths.
ARG DEBIAN_POOL=https://archive.debian.org/debian
ARG DEBIAN_POOL_FALLBACK=https://snapshot.debian.org/archive/debian/20250129T203412Z

# llvm.sh needs lsb_release, wget and gpg. It does not need add-apt-repository
# on trixie (software-properties-common no longer exists there); it writes a
# deb822 .sources file directly instead.
RUN apt-get update && apt-get -y install \
      cmake make git curl golang libtool autoconf automake wget perl binutils \
      lsb-release gnupg

# Install clang before anything is compiled so that every native build in this
# image -- CMake, OpenSSL and libunwind alike -- uses the same compiler.
RUN wget https://apt.llvm.org/llvm.sh && \
  chmod +x llvm.sh && \
  ./llvm.sh 18

ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/llvm-18/bin/

# ---------------------------------------------------------------------------
# Debian 11 (bullseye, glibc 2.31) sysroot.
#
# The base image supplies the build tools; this sysroot supplies the target ABI.
# Everything is compiled and linked against it, so the glibc symbol versions the
# released .so files require are decided here rather than by whatever the base
# image happens to ship. Bumping `FROM` above therefore cannot raise the floor
# for users; build/tools/check-glibc-abi.sh enforces that.
#
# Packages are pinned by sha256 in build/sysroot/bullseye-<arch>.sha256 and
# fetched straight from the pool, with no apt and no signing keys involved. That
# is deliberate: bullseye's live suites are being retired, and its
# bullseye-security pool has already had .deb files pruned while still being
# advertised by the index -- which is what breaks a bullseye-based build.
# ---------------------------------------------------------------------------
COPY build/sysroot /tmp/sysroot-lists
RUN set -eu; \
    arch="$(dpkg --print-architecture)"; \
    list="/tmp/sysroot-lists/bullseye-${arch}.sha256"; \
    mkdir -p "${SYSROOT}"; \
    cd /tmp; \
    while read -r sha path; do \
        [ -n "${sha}" ] || continue; \
        curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
             -o pkg.deb "${DEBIAN_POOL}/${path}" || \
        curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
             -o pkg.deb "${DEBIAN_POOL_FALLBACK}/${path}"; \
        echo "${sha}  pkg.deb" | sha256sum -c -; \
        dpkg-deb -x pkg.deb "${SYSROOT}"; \
        rm -f pkg.deb; \
    done < "${list}"; \
    rm -rf /tmp/sysroot-lists

# Debian's -dev packages ship absolute symlinks. Inside a sysroot those resolve
# against the host root and would silently pull in the base image's glibc, so
# rewrite them as relative. Chromium's sysroot_creator.py does the same.
# Absolute paths inside ld scripts such as libc.so are fine: ld rewrites those
# through --sysroot.
RUN set -eu; \
    find "${SYSROOT}" -type l | while read -r link; do \
        target="$(readlink "${link}")"; \
        case "${target}" in \
          /*) ln -sfn "$(realpath -m --relative-to="$(dirname "${link}")" "${SYSROOT}${target}")" "${link}" ;; \
        esac; \
    done; \
    if find "${SYSROOT}" -xtype l | grep -q .; then \
        echo "sysroot has dangling symlinks:" >&2; find "${SYSROOT}" -xtype l >&2; exit 1; \
    fi; \
    test -f "${SYSROOT}/usr/include/features.h"; \
    test -f $(echo ${SYSROOT}/usr/lib/gcc/*-linux-gnu/10/libstdc++.a)

# Build OpenSSL from source with static libs, against the sysroot so its
# objects cannot reference glibc symbols newer than the floor.
ARG OPENSSL_VERSION=3.5.8
RUN wget -q "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./config no-shared no-tests --prefix=/usr/local/openssl --openssldir=/etc/ssl \
        CC=clang CFLAGS="--sysroot=${SYSROOT}" LDFLAGS="--sysroot=${SYSROOT}" && \
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

# CMAKE_SYSROOT puts --sysroot on every compile and link line, including the
# third-party trees added via add_subdirectory/FetchContent. --gcc-install-dir
# pins clang to the sysroot's libstdc++ headers and libstdc++.a rather than the
# base image's; it goes in CMAKE_{C,CXX}_FLAGS because CMake uses those on link
# lines too.
RUN mkdir build-${CMAKE_BUILD_TYPE} && \
    cd build-${CMAKE_BUILD_TYPE} && \
    GCC_DIR="$(echo ${SYSROOT}/usr/lib/gcc/*-linux-gnu/10)" && \
    cmake .. \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_SYSROOT=${SYSROOT} \
        -DCMAKE_C_FLAGS="--gcc-install-dir=${GCC_DIR}" \
        -DCMAKE_CXX_FLAGS="--gcc-install-dir=${GCC_DIR}" \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
        -DCMAKE_C_FLAGS_DEBUG="-g -O0" \
        -DOPENSSL_ROOT_DIR=/usr/local/openssl

RUN cd build-${CMAKE_BUILD_TYPE} && make -j16 Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64

# Fail the build -- not just the release -- if the glibc ABI floor regresses.
RUN build/tools/check-glibc-abi.sh \
        artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so \
        artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so

FROM build AS test

# ARG values do not cross stage boundaries, so CMAKE_BUILD_TYPE has to be
# redeclared here or the build directory name below comes out as "build-".
ARG CMAKE_BUILD_TYPE=Release

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
