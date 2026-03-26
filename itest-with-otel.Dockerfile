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
RUN sed -i -E 's|<TargetFrameworks>.*</TargetFrameworks>|<TargetFramework>net'$SDK_VERSION'</TargetFramework>|' ./app/Rideshare.csproj

WORKDIR /dotnet/app

# Debug: verify csproj and source files
RUN echo "=== SDK version ===" && dotnet --version && echo "=== Rideshare.csproj ===" && cat Rideshare.csproj && echo "=== Program.cs ===" && cat Program.cs && echo "=== ls ===" && ls -la

# We hardcode linux-x64 here, as the profiler doesn't support any other platform
RUN dotnet publish -o . --framework net$SDK_VERSION --runtime linux-x64 --no-self-contained -v:d 2>&1 | tail -100

# This uses a locally built image of the SDK
FROM --platform=linux/amd64 $PYROSCOPE_SDK_IMAGE AS sdk
ARG PYROSCOPE_SDK_IMAGE

# Runtime only image of the targetplatfrom, so the platform the image will be running on.
FROM --platform=linux/amd64 mcr.microsoft.com/dotnet/aspnet:$SDK_VERSION$SDK_IMAGE_SUFFIX

RUN if command -v apt-get > /dev/null 2>&1; then \
        apt-get update && apt-get install -y --no-install-recommends curl unzip && rm -rf /var/lib/apt/lists/*; \
    else \
        apk add --no-cache curl unzip; \
    fi

ARG OTEL_VERSION=1.14.1
ENV OTEL_DOTNET_AUTO_HOME=/opt/otel-dotnet
RUN mkdir -p ${OTEL_DOTNET_AUTO_HOME} && \
    curl -sSfL https://github.com/open-telemetry/opentelemetry-dotnet-instrumentation/releases/download/v${OTEL_VERSION}/otel-dotnet-auto-install.sh -o /tmp/otel-install.sh && \
    sh /tmp/otel-install.sh && \
    rm /tmp/otel-install.sh

# OTEL as the main (classic) profiler
ENV CORECLR_ENABLE_PROFILING=1
ENV CORECLR_PROFILER={918728DD-259F-4A6A-AC2B-B85E1B658318}
ENV CORECLR_PROFILER_PATH=${OTEL_DOTNET_AUTO_HOME}/linux-x64/OpenTelemetry.AutoInstrumentation.Native.so
ENV DOTNET_ADDITIONAL_DEPS=${OTEL_DOTNET_AUTO_HOME}/AdditionalDeps
ENV DOTNET_SHARED_STORE=${OTEL_DOTNET_AUTO_HOME}/store
ENV DOTNET_STARTUP_HOOKS=${OTEL_DOTNET_AUTO_HOME}/net/OpenTelemetry.AutoInstrumentation.StartupHook.dll

# Disable OTEL exporters — we only need OTEL to occupy the profiler slot
ENV OTEL_TRACES_EXPORTER=none
ENV OTEL_METRICS_EXPORTER=none
ENV OTEL_LOGS_EXPORTER=none

WORKDIR /dotnet

# Pyroscope as the notification profiler
COPY --from=sdk /Pyroscope.Profiler.Native.so ./subfolder/Pyroscope.Profiler.Native.so
COPY --from=sdk /Pyroscope.Linux.ApiWrapper.x64.so ./subfolder/Pyroscope.Linux.ApiWrapper.x64.so
COPY --from=build /dotnet/app ./

ENV LD_LIBRARY_PATH=/dotnet/subfolder/
ENV CORECLR_ENABLE_NOTIFICATION_PROFILERS=1
ENV CORECLR_NOTIFICATION_PROFILERS=/dotnet/subfolder/Pyroscope.Profiler.Native.so={BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}
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
