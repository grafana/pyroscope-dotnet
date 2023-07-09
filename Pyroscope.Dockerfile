FROM debian:10 as builder

RUN apt-get update && apt-get -y install cmake clang make git curl golang libtool

WORKDIR /profiler

ADD build build
ADD profiler profiler
ADD tracer tracer
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt

RUN mkdir build-release && cd build-release && cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
RUN cd build-release && make -j16 Datadog.Profiler.Native Datadog.Linux.ApiWrapper.x64

FROM scratch
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux/Datadog.Profiler.Native.so /Pyroscope.Profiler.Native.so
COPY --from=builder /profiler/profiler/_build/DDProf-Deploy/linux/Datadog.Linux.ApiWrapper.x64.so /Pyroscope.Linux.ApiWrapper.x64.so