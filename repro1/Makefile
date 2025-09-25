
# Variables
PROJECT_NAME = PyroscopeApp
DOTNET_VERSION = 8.0
BUILD_CONFIG = Debug
PUBLISH_CONFIG = Release

# Default target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  build          - Build the application in Debug mode"
	@echo "  build-release  - Build the application in Release mode"
	@echo "  run            - Run the application locally"
	@echo "  run-dev        - Run the application with development settings"
	@echo "  clean          - Clean build artifacts"
	@echo "  restore        - Restore NuGet packages"
	@echo "  publish        - Publish the application"
	@echo "  docker-build   - Build Docker image"
	@echo "  docker-run     - Run with Docker Compose"
	@echo "  docker-stop    - Stop Docker containers"
	@echo "  docker-clean   - Clean Docker resources"
	@echo "  test-endpoints - Test application endpoints"
	@echo "  extract-dlls   - Extract Pyroscope native DLLs for local dev"
	@echo "  dev-setup      - Setup for local development"

# Local development targets
.PHONY: extract-dlls
extract-dlls:
	@echo "Extracting Pyroscope native libraries from Docker image..."
	@mkdir -p runtimes/linux-x64/native
	@docker create --name pyroscope-temp pyroscope/pyroscope-dotnet:0.12.0-glibc > /dev/null 2>&1 || true
	@docker cp pyroscope-temp:/Pyroscope.Profiler.Native.so runtimes/linux-x64/native/ 2>/dev/null || echo "Warning: Could not extract Pyroscope.Profiler.Native.so"
	@docker cp pyroscope-temp:/Pyroscope.Linux.ApiWrapper.x64.so runtimes/linux-x64/native/ 2>/dev/null || echo "Warning: Could not extract Pyroscope.Linux.ApiWrapper.x64.so"
	@docker rm pyroscope-temp > /dev/null 2>&1 || true
	@echo "Native libraries extracted to runtimes/linux-x64/native/"
	@ls -la runtimes/linux-x64/native/ 2>/dev/null || echo "Directory not found"

.PHONY: restore
restore:
	dotnet restore

.PHONY: build
build: restore
	dotnet build --configuration $(BUILD_CONFIG) --no-restore

.PHONY: build-release
build-release: restore
	dotnet build --configuration $(PUBLISH_CONFIG) --no-restore

.PHONY: clean
clean:
	dotnet clean
	rm -rf bin/ obj/ publish/ runtimes/

.PHONY: run
run: build
	dotnet run --configuration $(BUILD_CONFIG)

.PHONY: run-dev
run-dev: export ASPNETCORE_ENVIRONMENT=Development
run-dev: export ASPNETCORE_URLS=http://localhost:5000
run-dev: export PYROSCOPE_APPLICATION_NAME=napiv1-dev
run-dev: export PYROSCOPE_LABELS=environment:dev,region:local
run-dev: export PYROSCOPE_PROFILING_ENABLED=1
run-dev: export PYROSCOPE_LOG_LEVEL=debug
run-dev: export PYROSCOPE_PROFILING_ALLOCATION_ENABLED=true
run-dev: export PYROSCOPE_PROFILING_CONTENTION_ENABLED=true
run-dev: export PYROSCOPE_PROFILING_EXCEPTION_ENABLED=true
run-dev: export PYROSCOPE_SERVER_ADDRESS=http://localhost:4040
run-dev: export CORECLR_ENABLE_PROFILING=1
run-dev: export CORECLR_PROFILER={BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}
run-dev: export CORECLR_PROFILER_PATH=$(PWD)/runtimes/linux-x64/native/Pyroscope.Profiler.Native.so
run-dev: export LD_PRELOAD=$(PWD)/runtimes/linux-x64/native/Pyroscope.Linux.ApiWrapper.x64.so
run-dev: check-dlls run

.PHONY: check-dlls
check-dlls:
	@if [ ! -f "runtimes/linux-x64/native/Pyroscope.Profiler.Native.so" ] || [ ! -f "runtimes/linux-x64/native/Pyroscope.Linux.ApiWrapper.x64.so" ]; then \
		echo "Pyroscope native libraries not found. Run 'make extract-dlls' first."; \
		echo "Missing files:"; \
		[ ! -f "runtimes/linux-x64/native/Pyroscope.Profiler.Native.so" ] && echo "  - Pyroscope.Profiler.Native.so"; \
		[ ! -f "runtimes/linux-x64/native/Pyroscope.Linux.ApiWrapper.x64.so" ] && echo "  - Pyroscope.Linux.ApiWrapper.x64.so"; \
		exit 1; \
	fi

.PHONY: publish
publish: restore
	dotnet publish --configuration $(PUBLISH_CONFIG) --output publish/ --no-restore

# Docker targets
.PHONY: docker-build
docker-build:
	docker compose build

.PHONY: docker-run
docker-run:
	docker compose up --build -d

.PHONY: docker-stop
docker-stop:
	docker compose down

.PHONY: docker-clean
docker-clean:
	docker compose down -v --rmi all
	docker system prune -f

.PHONY: docker-logs
docker-logs:
	docker compose logs -f

# Development setup
.PHONY: dev-setup
dev-setup:
	@echo "Setting up development environment..."
	@which dotnet > /dev/null || (echo "Please install .NET $(DOTNET_VERSION) SDK" && exit 1)
	@echo ".NET SDK version:" && dotnet --version
	$(MAKE) restore
	$(MAKE) extract-dlls
	@echo "Development setup complete!"

# Testing targets
.PHONY: test-endpoints
test-endpoints:
	@echo "Testing application endpoints..."
	@echo "Health check:" && curl -s http://localhost:5000/Pyroscope/Health || echo "App not running on port 5000"
	@echo "Health check (Docker):" && curl -s http://localhost:8080/Pyroscope/Health || echo "App not running on port 8080"

.PHONY: load-test
load-test:
	@echo "Generating load for profiling data..."
	@for i in $$(seq 1 10); do \
		echo "Request $$i:"; \
		curl -s http://localhost:5000/Pyroscope/Test 2>/dev/null || curl -s http://localhost:8080/Pyroscope/Test 2>/dev/null || echo "No app running"; \
		sleep 1; \
	done

# Utility targets
.PHONY: check-dotnet
check-dotnet:
	@dotnet --version || (echo "Please install .NET $(DOTNET_VERSION) SDK" && exit 1)

.PHONY: info
info:
	@echo "Project: $(PROJECT_NAME)"
	@echo "Target Framework: net$(DOTNET_VERSION)"
	@echo "Build Configuration: $(BUILD_CONFIG)"
	@echo "Publish Configuration: $(PUBLISH_CONFIG)"