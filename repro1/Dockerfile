# Use the official .NET 8 SDK image to build the application
FROM mcr.microsoft.com/dotnet/sdk:8.0 AS build
WORKDIR /src

# Copy project file and restore dependencies
COPY *.csproj ./
RUN dotnet restore

# Copy all source code and build the application
COPY . ./
RUN dotnet publish -c Release -o /app/publish

# Use the official .NET 8 runtime image for the final stage
FROM mcr.microsoft.com/dotnet/aspnet:8.0 AS final

# Create app user and directory (handle existing group gracefully)
RUN groupadd -g 1000 app || true && useradd -r -u 1000 -g app app || true
WORKDIR /app

# Copy the Pyroscope libraries from the official pyroscope-dotnet image (glibc variant)
COPY --from=pyroscope/pyroscope-dotnet:0.12.0-glibc /Pyroscope.Profiler.Native.so /app/runtimes/linux-x64/native/Pyroscope.Profiler.Native.so
COPY --from=pyroscope/pyroscope-dotnet:0.12.0-glibc /Pyroscope.Linux.ApiWrapper.x64.so /app/runtimes/linux-x64/native/Pyroscope.Linux.ApiWrapper.x64.so

# Create log directory for Pyroscope
RUN mkdir -p /var/log/pyroscope/dotnet && chown -R app:app /var/log/pyroscope

# Set Pyroscope environment variables
ENV CORECLR_PROFILER_PATH="/app/runtimes/linux-x64/native/Pyroscope.Profiler.Native.so" \
    LD_PRELOAD="/app/runtimes/linux-x64/native/Pyroscope.Linux.ApiWrapper.x64.so" \
    CORECLR_ENABLE_PROFILING=1 \
    CORECLR_PROFILER="{BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}" \
    PYROSCOPE_PROFILING_ENABLED=1 \
    PYROSCOPE_PROFILING_ALLOCATION_ENABLED=true \
    PYROSCOPE_PROFILING_CONTENTION_ENABLED=true \
    PYROSCOPE_PROFILING_EXCEPTION_ENABLED=true \
    PYROSCOPE_LOG_LEVEL=debug

# Copy published application
COPY --from=build /app/publish .

# Change ownership to app user
RUN chown -R app:app /app

# Switch to non-root user
USER app

# Expose port
EXPOSE 5000

# Start the application
ENTRYPOINT ["dotnet", "PyroscopeApp.dll"]