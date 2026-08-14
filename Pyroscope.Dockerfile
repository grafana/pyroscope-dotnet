FROM debian:bullseye-20260406@sha256:bf53effcacca31b60ce97dabc67578f37e43075d716dc90804d3da3a80d2996c AS builder

# deb.debian.org (Fastly) intermittently resets connections on cold CI builds; retry apt fetches.
RUN echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries

RUN apt-get update && apt-get -y install cmake make git curl golang libtool wget perl

# Build OpenSSL from source with static libs
ARG OPENSSL_VERSION=3.5.7
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
ADD --exclude=test/Datadog.Profiler.IntegrationTests --exclude=src/Demos profiler profiler
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

FROM mcr.microsoft.com/dotnet/sdk:10.0.203 AS profiler-integration-test-runner

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gdb \
        libldap2 \
        procps && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /repo
COPY . .
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so /opt/pyroscope/Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /opt/pyroscope/Pyroscope.Linux.ApiWrapper.x64.so

RUN for project in \
        profiler/src/Demos/Samples.BuggyBits/Samples.BuggyBits.csproj \
        profiler/src/Demos/Samples.Computer01/Samples.Computer01.csproj \
        profiler/src/Demos/Samples.ExceptionGenerator/Samples.ExceptionGenerator.csproj \
        profiler/src/Demos/Samples.HttpRequest/Samples.HttpRequest.csproj \
        profiler/src/Demos/Samples.ParallelCountSites/Samples.ParallelCountSites.csproj \
        profiler/src/Demos/Samples.WaitHandles/Samples.WaitHandles.csproj \
        profiler/src/Demos/Samples.Website-AspNetCore01/Samples.Website-AspNetCore01.csproj; \
    do dotnet build "$project" -c Release -f net10.0 -p:Platform=x64 || exit; done && \
    dotnet build profiler/test/Datadog.Profiler.IntegrationTests/Datadog.Profiler.IntegrationTests.csproj \
        -c Release -p:Platform=x64 && \
    chmod +x profiler/test/Datadog.Profiler.IntegrationTests/run-integration-tests.sh

ENV CI=true \
    PYROSCOPE_PROFILER_PATH=/opt/pyroscope/Pyroscope.Profiler.Native.so \
    PYROSCOPE_API_WRAPPER_PATH=/opt/pyroscope/Pyroscope.Linux.ApiWrapper.x64.so \
    DD_TESTING_OUPUT_DIR=/results/output

ENTRYPOINT ["profiler/test/Datadog.Profiler.IntegrationTests/run-integration-tests.sh"]

FROM busybox:1.38.0-glibc@sha256:3ba030337caebbfc2232b22b1e435eb213b28e5844a34942c74555bf904a265a
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/artifacts/profiler-build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

