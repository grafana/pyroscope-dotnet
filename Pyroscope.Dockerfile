FROM debian:11 AS builder


RUN apt-get update && apt-get -y install cmake make git curl golang libtool wget

RUN apt-get -y install lsb-release wget software-properties-common gnupg

RUN wget https://apt.llvm.org/llvm.sh && \
  chmod +x llvm.sh && \
  ./llvm.sh 18

WORKDIR /profiler

ADD build build
ADD profiler profiler
ADD tracer tracer
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

# Allow build type to be passed as build arg, default to Release
ARG CMAKE_BUILD_TYPE=Release
ENV PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/llvm-18/bin/

RUN mkdir build-${CMAKE_BUILD_TYPE} && \
    cd build-${CMAKE_BUILD_TYPE} && \
    cmake .. \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
        -DCMAKE_C_FLAGS_DEBUG="-g -O0"

RUN cd build-${CMAKE_BUILD_TYPE} && make -j16 Datadog.Profiler.Native Datadog.Linux.ApiWrapper.x64

FROM busybox:1.36.1-glibc
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux/Datadog.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

