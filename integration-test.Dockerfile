ARG SDK_VERSION=8.0
ARG SDK_IMAGE_SUFFIX
ARG DOTNET_RUNTIME_ID=linux-x64
FROM mcr.microsoft.com/dotnet/sdk:$SDK_VERSION$SDK_IMAGE_SUFFIX AS build

ARG SDK_VERSION
ARG SDK_IMAGE_SUFFIX
ARG DOTNET_RUNTIME_ID

WORKDIR /dotnet

COPY IntegrationTest ./app
COPY Pyroscope/Directory.Build.props ./Pyroscope/Directory.Build.props
COPY Pyroscope/package-logo.png ./Pyroscope/package-logo.png
COPY Pyroscope/Pyroscope ./Pyroscope/Pyroscope

# Set the target framework to SDK_VERSION
RUN sed -i -E 's|<TargetFrameworks>.*</TargetFrameworks>|<TargetFramework>net'$SDK_VERSION'</TargetFramework>|' ./app/Rideshare.csproj

WORKDIR /dotnet/app

# Publish to a separate directory: .NET 10+ cleans the output dir before compiling,
# so -o . (source dir) would delete source files and cause CS5001.
RUN dotnet publish -o /dotnet/publish --framework net$SDK_VERSION --runtime $DOTNET_RUNTIME_ID --no-self-contained

# Runtime-only image of the target platform.
FROM mcr.microsoft.com/dotnet/aspnet:$SDK_VERSION$SDK_IMAGE_SUFFIX
ARG PROFILER_BINARIES_DIR=profiler-bin

WORKDIR /dotnet

# place the binaries in a subfolder - to rigger a problme when SONAME was Datadog.Profiler.Native
# and dynamic linker could not find the profiler lib.
COPY ${PROFILER_BINARIES_DIR}/Pyroscope.Profiler.Native.so ./subfolder/Pyroscope.Profiler.Native.so
COPY ${PROFILER_BINARIES_DIR}/Pyroscope.Linux.ApiWrapper.x64.so ./subfolder/Pyroscope.Linux.ApiWrapper.x64.so
COPY --from=build /dotnet/publish ./

# Fix for alpine not being able to dlopen an already loaded library
ENV LD_LIBRARY_PATH=/dotnet/subfolder/

ENV CORECLR_ENABLE_PROFILING=1
ENV CORECLR_PROFILER={BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}
ENV CORECLR_PROFILER_PATH=/dotnet/subfolder/Pyroscope.Profiler.Native.so
ENV LD_PRELOAD=/dotnet/subfolder/Pyroscope.Linux.ApiWrapper.x64.so

ENV PYROSCOPE_SERVER_ADDRESS=http://pyroscope:4040
ENV PYROSCOPE_LOG_LEVEL=debug
ENV PYROSCOPE_PROFILING_ENABLED=1
ENV PYROSCOPE_PROFILING_ALLOCATION_ENABLED=true
ENV PYROSCOPE_PROFILING_CONTENTION_ENABLED=true
ENV PYROSCOPE_PROFILING_EXCEPTION_ENABLED=true
ENV PYROSCOPE_PROFILING_HEAP_ENABLED=true
ENV RIDESHARE_LISTEN_PORT=5000


CMD sh -c "ASPNETCORE_URLS=http://*:${RIDESHARE_LISTEN_PORT} exec dotnet /dotnet/rideshare.dll"
