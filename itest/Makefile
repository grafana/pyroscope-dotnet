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

.PHONY: ci-matrix
ci-matrix:
	@go test -list . -json ./... | jq -sc '[.[] | select(.Action=="output") | .Output | rtrimstr("\n") | select(startswith("Test"))]'

.PHONY: test/%
test/%:
	go test -v -timeout 60m -count=1 -run '^$*$$' ./...
