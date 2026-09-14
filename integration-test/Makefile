REPO_ROOT := $(shell git rev-parse --show-toplevel)
PROFILER_BINARIES_DIR ?= $(REPO_ROOT)/profiler-bin

.PHONY: check-profiler-binaries
check-profiler-binaries:
	test -f $(PROFILER_BINARIES_DIR)/Pyroscope.Profiler.Native.so
	test -f $(PROFILER_BINARIES_DIR)/Pyroscope.Linux.ApiWrapper.x64.so

.PHONY: ci-matrix
ci-matrix:
	@go test -list . -json . | jq -sc '[.[] | select(.Action=="output") | .Output | rtrimstr("\n") | select(startswith("Test"))]'

.PHONY: test/%
test/%:
	go test -v -timeout 60m -count=1 -run '^$*$$' ./...
