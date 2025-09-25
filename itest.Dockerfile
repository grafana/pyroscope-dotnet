ARG SDK_VERSION=8.0
ARG PYROSCOPE_SDK_IMAGE
ARG SDK_IMAGE_SUFFIX
# The build images takes an SDK image of the buildplatform, so the platform the build is running on.
FROM --platform=$BUILDPLATFORM mcr.microsoft.com/dotnet/sdk:$SDK_VERSION$SDK_IMAGE_SUFFIX AS build

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG SDK_VERSION
ARG SDK_IMAGE_SUFFIX

WORKDIR /dotnet

COPY IntegrationTest ./app
COPY Pyroscope/Directory.Build.props ./Pyroscope/Directory.Build.props
COPY Pyroscope/package-logo.png ./Pyroscope/package-logo.png
COPY Pyroscope/Pyroscope ./Pyroscope/Pyroscope

# Set the target framework to SDK_VERSION
RUN sed -i -E 's|<TargetFramework>.*</TargetFramework>|<TargetFramework>net'$SDK_VERSION'</TargetFramework>|' ./app/Rideshare.csproj

WORKDIR /dotnet/app

# We hardcode linux-x64 here, as the profiler doesn't support any other platform
RUN dotnet publish -o . --framework net$SDK_VERSION --runtime linux-x64 --no-self-contained

# This uses a locally built image of the SDK
FROM --platform=linux/amd64 $PYROSCOPE_SDK_IMAGE AS sdk
ARG PYROSCOPE_SDK_IMAGE

# Runtime only image of the targetplatfrom, so the platform the image will be running on.
FROM --platform=linux/amd64 mcr.microsoft.com/dotnet/aspnet:$SDK_VERSION$SDK_IMAGE_SUFFIX

WORKDIR /dotnet

# place the binaries in a subfolder - to rigger a problme when SONAME was Datadog.Profiler.Native
# and dynamic linker could not find the profiler lib.
COPY --from=sdk /Pyroscope.Profiler.Native.so ./subfolder/Pyroscope.Profiler.Native.so
COPY --from=sdk /Pyroscope.Linux.ApiWrapper.x64.so ./subfolder/Pyroscope.Linux.ApiWrapper.x64.so
COPY --from=build /dotnet/app ./


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
