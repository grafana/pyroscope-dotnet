FROM debian:10

RUN apt-get update \
    && apt-get -y install wget apt-transport-https\
    && wget https://packages.microsoft.com/config/debian/10/packages-microsoft-prod.deb -O packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb\
    && rm packages-microsoft-prod.deb


RUN apt-get update && apt-get -y install cmake clang make git curl golang libtool dotnet-sdk-6.0

WORKDIR /profiler
##
ADD build build
ADD profiler profiler
ADD tracer tracer
ADD shared shared
ADD CMakeLists.txt CMakeLists.txt
#
RUN mkdir build-release && cd build-release && cmake .. -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release

RUN cd build-release && make -j6 Datadog.Profiler.Native Datadog.Linux.ApiWrapper.x64