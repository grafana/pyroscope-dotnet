REPO_ROOT := $(shell git rev-parse --show-toplevel)
GOARCH := $(shell go env GOARCH)
BASE_ARCH_amd64 := x86_64
BASE_ARCH_arm64 := aarch64
BASE_ARCH := $(BASE_ARCH_$(GOARCH))

# Build profiler images
.PHONY: build-profiler-glibc
build-profiler-glibc:
	docker build --platform linux/$(GOARCH) --build-arg BASE_ARCH=$(BASE_ARCH) -t pyroscope-dotnet-glibc:test \
		-f $(REPO_ROOT)/Pyroscope.Dockerfile \
		--build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)

.PHONY: build-profiler-musl
build-profiler-musl:
	docker build --platform linux/$(GOARCH) --build-arg BASE_ARCH=$(BASE_ARCH) -t pyroscope-dotnet-musl:test \
		-f $(REPO_ROOT)/Pyroscope.musl.Dockerfile \
		--build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)

.PHONY: ci-matrix
ci-matrix:
	@go test -list . -json . | jq -sc '[.[] | select(.Action=="output") | .Output | rtrimstr("\n") | select(startswith("Test"))]'

.PHONY: test/%
test/%:
	go test -v -timeout 60m -count=1 -run '^$*$$' ./...
