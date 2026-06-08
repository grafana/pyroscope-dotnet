LIBC ?= glibc
ARCH ?= x86_64
# Defaults to the profiler version tracked by release-please. The release
# workflow overrides it on the command line with the version being released.
RELEASE_VERSION ?= $(shell jq -r '."."' .release-please-manifest.json)

ifeq ($(RELEASE_VERSION),)
  $(error RELEASE_VERSION is required but could not be read from .release-please-manifest.json)
endif

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

.PHONY: docker/archive
docker/archive:
	docker build -f $(DOCKERFILE) -o out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)  .
	cd out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH) && tar -czvf ../pyroscope.$(RELEASE_VERSION)-$(LIBC)-$(ARCH).tar.gz *.so 
	rm -rf out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)


include dev.mk