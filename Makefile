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

# manylinux2014 (CentOS 7, glibc 2.17) keeps the shipped .so runnable on old distros.
# See the comment at the top of Pyroscope.Dockerfile.
BASE_IMAGE_x86_64 := quay.io/pypa/manylinux2014_x86_64@sha256:493d2032114d757aaa761a9385ad8497f391503bf71acef9abeeb66682ca5d90
BASE_IMAGE_aarch64 := quay.io/pypa/manylinux2014_aarch64@sha256:f4cd164263e4ec2b7da7ee40b319bb5e30f0d7a2abd7ad4730e716a512dfb529
BASE_IMAGE := $(BASE_IMAGE_$(ARCH))

ifeq ($(ARCH),x86_64)
else ifeq ($(ARCH),aarch64)
else
    $(error ARCH must be either x86_64, aarch64)
endif

.PHONY: docker/archive
docker/archive:
	docker build -f $(DOCKERFILE) \
		$(if $(filter glibc,$(LIBC)),--build-arg BASE_IMAGE=$(BASE_IMAGE),) \
		-o out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH) .
	cd out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH) && tar -czvf ../pyroscope.$(RELEASE_VERSION)-$(LIBC)-$(ARCH).tar.gz *.so 
	rm -rf out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)


include dev.mk