LIBC ?= glibc
ARCH ?= x86_64
RELEASE_VERSION ?=
DOCKER_IMAGE ?= pyroscope/pyroscope-dotnet

ifeq ($(RELEASE_VERSION),)
  $(error "no release version specified")
endif
RELEASE_VERSION_TMP := $(shell echo $(RELEASE_VERSION) | sed -E 's/^v([0-9]+\.[0-9]+\.[0-9]+)(-pyroscope)?$$/\1/')
#$(error "debug $(RELEASE_VERSION_TMP)")
RELEASE_VERSION := $(RELEASE_VERSION_TMP)

ifeq ($(LIBC),musl)
	DOCKERFILE := Pyroscope.musl.Dockerfile
else ifeq ($(LIBC),glibc)
	DOCKERFILE := Pyroscope.Dockerfile
else
    $(error LIBC must be either musl or glibc)
endif

ifeq ($(ARCH),x86_64)
else ifeq ($(ARCH),aarch64)
else
    $(error ARCH must be either x86_64, aarch64)
endif

.phony: docker/build
docker/build:
	docker run --rm  debian:10 uname -a
	docker run --rm  debian:10 uname -a | grep $(ARCH)
	docker build -f $(DOCKERFILE) -t $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-$(ARCH) .

.phony: docker/archive
docker/archive:
	docker build -f $(DOCKERFILE) -o out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)  .
	cd out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH) && tar -czvf ../pyroscope.$(RELEASE_VERSION)-$(LIBC)-$(ARCH).tar.gz *.so 
	rm -rf out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)

.phony: docker/push
docker/push:
	docker push $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-$(ARCH)

.phony: docker/manifest
docker/manifest:
	docker manifest create \
		$(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)                 \
		--amend $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-x86_64   \
		--amend $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-aarch64 
	docker manifest push $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)

	docker manifest create \
		$(DOCKER_IMAGE):latest-$(LIBC)                 \
		--amend $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-x86_64   \
		--amend $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC)-aarch64 
	docker manifest push $(DOCKER_IMAGE):latest-$(LIBC)


.phony: bump_version
bump_version:
	sed -i "Pyroscope/Pyroscope/Pyroscope.csproj" -e "s/<PackageVersion>.*<\/PackageVersion>/<PackageVersion>$(RELEASE_VERSION)<\/PackageVersion>/"
	sed -i "Pyroscope/Pyroscope/Pyroscope.csproj" -e "s/<AssemblyVersion>.*<\/AssemblyVersion>/<AssemblyVersion>$(RELEASE_VERSION)<\/AssemblyVersion>/"
	sed -i "Pyroscope/Pyroscope/Pyroscope.csproj" -e "s/<FileVersion>.*<\/FileVersion>/<FileVersion>$(RELEASE_VERSION)<\/FileVersion>/"
	sed -i "profiler/src/ProfilerEngine/Datadog.Profiler.Native/PyroscopePprofSink.h" -e "s/#define PYROSCOPE_SPY_VERSION \".*\"/#define PYROSCOPE_SPY_VERSION \"$(RELEASE_VERSION)\"/"

