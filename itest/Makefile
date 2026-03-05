FLAVOUR ?= glibc
DOTNET_VERSION ?= 8.0
REPO_ROOT := $(CURDIR)/..

.PHONY: build-profiler build-app itest

build-profiler:
ifeq ($(FLAVOUR),musl)
	docker build --platform linux/amd64 -t pyroscope-dotnet-musl:test \
	  -f $(REPO_ROOT)/Pyroscope.musl.Dockerfile --build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)
else
	docker build --platform linux/amd64 -t pyroscope-dotnet-glibc:test \
	  -f $(REPO_ROOT)/Pyroscope.Dockerfile --build-arg CMAKE_BUILD_TYPE=Debug $(REPO_ROOT)
endif

build-app: build-profiler
ifeq ($(FLAVOUR),glibc)
	docker build --platform linux/amd64 \
	  --build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-$(FLAVOUR):test \
	  --build-arg SDK_VERSION=$(DOTNET_VERSION) \
	  --build-arg SDK_IMAGE_SUFFIX=-jammy \
	  -t rideshare-app-$(FLAVOUR):net$(DOTNET_VERSION) \
	  -f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)
else
	docker build --platform linux/amd64 \
	  --build-arg PYROSCOPE_SDK_IMAGE=pyroscope-dotnet-$(FLAVOUR):test \
	  --build-arg SDK_VERSION=$(DOTNET_VERSION) \
	  --build-arg SDK_IMAGE_SUFFIX=-alpine \
	  -t rideshare-app-$(FLAVOUR):net$(DOTNET_VERSION) \
	  -f $(REPO_ROOT)/itest.Dockerfile $(REPO_ROOT)
endif

itest: build-app
	FLAVOUR=$(FLAVOUR) DOTNET_VERSION=$(DOTNET_VERSION) \
	  go test -v -timeout 15m -count=1 ./...
