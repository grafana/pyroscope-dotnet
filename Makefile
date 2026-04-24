LIBC ?= glibc
ARCH ?= x86_64
RELEASE_VERSION ?=
DOCKER_TAG_VERSION ?= $(RELEASE_VERSION)
DOCKER_IMAGE ?= pyroscope/pyroscope-dotnet

ifeq ($(RELEASE_VERSION),)
  $(error RELEASE_VERSION is required)
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

.PHONY: docker/build
docker/build:
	docker run --rm  debian:10 uname -a
	docker run --rm  debian:10 uname -a | grep $(ARCH)
	docker build -f $(DOCKERFILE) -t $(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)-$(ARCH) .

.PHONY: docker/archive
docker/archive:
	docker build -f $(DOCKERFILE) -o out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)  .
	cd out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH) && tar -czvf ../pyroscope.$(RELEASE_VERSION)-$(LIBC)-$(ARCH).tar.gz *.so 
	rm -rf out.$(RELEASE_VERSION)-$(LIBC)-$(ARCH)

.PHONY: docker/push
docker/push:
	docker push $(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)-$(ARCH)

.PHONY: docker/manifest
docker/manifest:
	docker manifest create \
		$(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)                 \
		--amend $(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)-x86_64   \
		--amend $(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)-aarch64
	docker manifest push $(DOCKER_IMAGE):$(DOCKER_TAG_VERSION)-$(LIBC)

.PHONY: docker/promote
docker/promote:
	docker buildx imagetools create --tag $(DOCKER_IMAGE):$(RELEASE_VERSION)-$(LIBC) $(DOCKER_IMAGE):$(RELEASE_VERSION)-draft-$(LIBC)
	docker buildx imagetools create --tag $(DOCKER_IMAGE):latest-$(LIBC) $(DOCKER_IMAGE):$(RELEASE_VERSION)-draft-$(LIBC)

