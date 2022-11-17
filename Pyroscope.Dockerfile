FROM ubuntu:22.04

RUN apt-get update && apt-get -y install cmake clang make git curl dotnet6 golang libtool

#WORKDIR /profiler
##
#ADD build build
#ADD profiler profiler
#ADD tracer tracer
#ADD shared shared
#ADD CMakeLists.txt CMakeLists.txt
##
#RUN mkdir build-release && cd build-release && cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo
#
#RUN cd build-release && make -j6 Datadog.Profiler.Native Datadog.Linux.ApiWrapper.x64