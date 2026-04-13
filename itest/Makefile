REPO_ROOT := $(shell git rev-parse --show-toplevel)

# Build profiler images
.PHONY: build-profiler-glibc
build-profiler-glibc:
	docker build --platform linux/amd64 -t pyroscope-dotnet-glibc:test \
		-f $(REPO_ROOT)/Pyroscope.Dockerfile \
		--build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)

.PHONY: build-profiler-musl
build-profiler-musl:
	docker build --platform linux/amd64 -t pyroscope-dotnet-musl:test \
		-f $(REPO_ROOT)/Pyroscope.musl.Dockerfile \
		--build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)

# Build app images - main profiler (regular)
.PHONY: build-app-main-profiler-glibc-6.0
build-app-main-profiler-glibc-6.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=6.0 \
		--build-arg SDK_IMAGE_SUFFIX=-jammy \
		-t rideshare-app-glibc:net6.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-glibc-8.0
build-app-main-profiler-glibc-8.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=8.0 \
		--build-arg SDK_IMAGE_SUFFIX=-jammy \
		-t rideshare-app-glibc:net8.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-musl-6.0
build-app-main-profiler-musl-6.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=6.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl:net6.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-musl-8.0
build-app-main-profiler-musl-8.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=8.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl:net8.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-glibc-9.0
build-app-main-profiler-glibc-9.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=9.0 \
		--build-arg SDK_IMAGE_SUFFIX=-noble \
		-t rideshare-app-glibc:net9.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-glibc-10.0
build-app-main-profiler-glibc-10.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=10.0 \
		--build-arg SDK_IMAGE_SUFFIX=-noble \
		-t rideshare-app-glibc:net10.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-musl-9.0
build-app-main-profiler-musl-9.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=9.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl:net9.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

.PHONY: build-app-main-profiler-musl-10.0
build-app-main-profiler-musl-10.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=10.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl:net10.0 \
		-f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)

# Build app images - notification profiler (with OTEL)
.PHONY: build-app-notification-profiler-glibc-6.0
build-app-notification-profiler-glibc-6.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=6.0 \
		--build-arg SDK_IMAGE_SUFFIX=-jammy \
		-t rideshare-app-glibc-otel:net6.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-glibc-8.0
build-app-notification-profiler-glibc-8.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=8.0 \
		--build-arg SDK_IMAGE_SUFFIX=-jammy \
		-t rideshare-app-glibc-otel:net8.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-musl-6.0
build-app-notification-profiler-musl-6.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=6.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl-otel:net6.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-musl-8.0
build-app-notification-profiler-musl-8.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=8.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl-otel:net8.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-glibc-9.0
build-app-notification-profiler-glibc-9.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=9.0 \
		--build-arg SDK_IMAGE_SUFFIX=-noble \
		-t rideshare-app-glibc-otel:net9.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-glibc-10.0
build-app-notification-profiler-glibc-10.0: build-profiler-glibc
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-glibc:test \
		--build-arg SDK_VERSION=10.0 \
		--build-arg SDK_IMAGE_SUFFIX=-noble \
		-t rideshare-app-glibc-otel:net10.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-musl-9.0
build-app-notification-profiler-musl-9.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=9.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl-otel:net9.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

.PHONY: build-app-notification-profiler-musl-10.0
build-app-notification-profiler-musl-10.0: build-profiler-musl
	docker build --platform linux/amd64 \
		--build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-musl:test \
		--build-arg SDK_VERSION=10.0 \
		--build-arg SDK_IMAGE_SUFFIX=-alpine \
		-t rideshare-app-musl-otel:net10.0 \
		-f $(REPO_ROOT)/itest-with-otel.Dockerfile $(REPO_ROOT)

# Integration test targets - main profiler
.PHONY: itest/main-profiler/glibc/6.0
itest/main-profiler/glibc/6.0: build-app-main-profiler-glibc-6.0
	FLAVOUR=glibc DOTNET_VERSION=6.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/glibc/8.0
itest/main-profiler/glibc/8.0: build-app-main-profiler-glibc-8.0
	FLAVOUR=glibc DOTNET_VERSION=8.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/musl/6.0
itest/main-profiler/musl/6.0: build-app-main-profiler-musl-6.0
	FLAVOUR=musl DOTNET_VERSION=6.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/musl/8.0
itest/main-profiler/musl/8.0: build-app-main-profiler-musl-8.0
	FLAVOUR=musl DOTNET_VERSION=8.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/glibc/9.0
itest/main-profiler/glibc/9.0: build-app-main-profiler-glibc-9.0
	FLAVOUR=glibc DOTNET_VERSION=9.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/glibc/10.0
itest/main-profiler/glibc/10.0: build-app-main-profiler-glibc-10.0
	FLAVOUR=glibc DOTNET_VERSION=10.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/musl/9.0
itest/main-profiler/musl/9.0: build-app-main-profiler-musl-9.0
	FLAVOUR=musl DOTNET_VERSION=9.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

.PHONY: itest/main-profiler/musl/10.0
itest/main-profiler/musl/10.0: build-app-main-profiler-musl-10.0
	FLAVOUR=musl DOTNET_VERSION=10.0 go test -v -timeout 15m -count=1 ./... -run TestRideshareProfiles

# Integration test targets - notification profiler (with OTEL)
.PHONY: itest/notification-profiler/glibc/6.0
itest/notification-profiler/glibc/6.0: build-app-notification-profiler-glibc-6.0
	FLAVOUR=glibc DOTNET_VERSION=6.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/glibc/8.0
itest/notification-profiler/glibc/8.0: build-app-notification-profiler-glibc-8.0
	FLAVOUR=glibc DOTNET_VERSION=8.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/musl/6.0
itest/notification-profiler/musl/6.0: build-app-notification-profiler-musl-6.0
	FLAVOUR=musl DOTNET_VERSION=6.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/musl/8.0
itest/notification-profiler/musl/8.0: build-app-notification-profiler-musl-8.0
	FLAVOUR=musl DOTNET_VERSION=8.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/glibc/9.0
itest/notification-profiler/glibc/9.0: build-app-notification-profiler-glibc-9.0
	FLAVOUR=glibc DOTNET_VERSION=9.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/glibc/10.0
itest/notification-profiler/glibc/10.0: build-app-notification-profiler-glibc-10.0
	FLAVOUR=glibc DOTNET_VERSION=10.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/musl/9.0
itest/notification-profiler/musl/9.0: build-app-notification-profiler-musl-9.0
	FLAVOUR=musl DOTNET_VERSION=9.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

.PHONY: itest/notification-profiler/musl/10.0
itest/notification-profiler/musl/10.0: build-app-notification-profiler-musl-10.0
	FLAVOUR=musl DOTNET_VERSION=10.0 OTEL=true go test -v -timeout 15m -count=1 ./... -run TestRideshareProfilesWithOTEL

# TLS profile upload tests — one per libc flavour, using the main (non-OTEL) profiler images.
.PHONY: itest/tls-profile-upload/glibc/8.0
itest/tls-profile-upload/glibc/8.0: build-app-main-profiler-glibc-8.0
	FLAVOUR=glibc DOTNET_VERSION=8.0 go test -v -timeout 10m -count=1 ./... -run TestTLSProfileUpload

.PHONY: itest/tls-profile-upload/musl/8.0
itest/tls-profile-upload/musl/8.0: build-app-main-profiler-musl-8.0
	FLAVOUR=musl DOTNET_VERSION=8.0 go test -v -timeout 10m -count=1 ./... -run TestTLSProfileUpload

# Dynamic profiling toggle tests — verifies disable/re-enable doesn't SIGSEGV (issue #259).
.PHONY: itest/dynamic-toggle/glibc/8.0
itest/dynamic-toggle/glibc/8.0: build-app-main-profiler-glibc-8.0
	FLAVOUR=glibc DOTNET_VERSION=8.0 go test -v -timeout 15m -count=1 ./... -run TestDynamicProfilingToggle

.PHONY: itest/dynamic-toggle/musl/8.0
itest/dynamic-toggle/musl/8.0: build-app-main-profiler-musl-8.0
	FLAVOUR=musl DOTNET_VERSION=8.0 go test -v -timeout 15m -count=1 ./... -run TestDynamicProfilingToggle
