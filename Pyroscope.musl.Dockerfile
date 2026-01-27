FROM alpine:3.15@sha256:19b4bcc4f60e99dd5ebdca0cbce22c503bbcff197549d7e19dab4f22254dc864 AS builder

RUN apk add  \
            clang \
            cmake \
            git \
            bash \
            make \
            alpine-sdk \
            util-linux-dev \
            autoconf \
            libtool \
            automake \
            xz-dev \
            musl-dbg

RUN apk add wget
RUN apk add go

FROM builder as build

WORKDIR /profiler
ENV IsAlpine=true

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
        -DCMAKE_C_FLAGS_DEBUG="-g -O0"

RUN cd build-${CMAKE_BUILD_TYPE} && make -j16 Pyroscope.Profiler.Native Datadog.Linux.ApiWrapper.x64

FROM busybox:1.36.1-musl@sha256:c29b078aeeb7cdc487cf52dc87a69dac71499937f498b35cb606c7943bc412c9
COPY --from=build /profiler/profiler/_build/DDProf-Deploy/linux-musl/Pyroscope.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=build /profiler/profiler/_build/DDProf-Deploy/linux-musl/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

