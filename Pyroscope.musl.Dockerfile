FROM alpine:3.15 AS builder

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


WORKDIR /profiler
ENV IsAlpine=true

ADD build build
ADD profiler profiler
ADD tracer tracer
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

RUN mkdir build-release && cd build-release && cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
RUN cd build-release && make -j16 Datadog.Profiler.Native Datadog.Linux.ApiWrapper.x64

FROM busybox:1.36.1-musl
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux-musl/Datadog.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux-musl/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so

