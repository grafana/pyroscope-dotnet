LIBC ?= glibc
ARCH ?= x86_64
RELEASE_VERSION ?= foo
DOCKER_TAG_VERSION ?= $(RELEASE_VERSION)
DOCKER_IMAGE ?= us-docker.pkg.dev/grafanalabs-global/dockerhub-pyroscope-dotnet-prod-mirror/pyroscope-dotnet

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


.PHONY: dev/apt
dev/apt:
	apt install make cmake clang libtool autoconf git gdb libssl-dev zlib1g-dev

.PHONY: dev/dotnet
dev/dotnet:
	curl -sSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
	chmod +x /tmp/dotnet-install.sh
	/tmp/dotnet-install.sh --channel 8.0
	/tmp/dotnet-install.sh --version 10.0.203
	dotnet --list-sdks
	rm /tmp/dotnet-install.sh

.PHONY: dev
dev:
	cmake -S . -B cmake-build-dev \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
            -DCMAKE_C_FLAGS_DEBUG="-g -O0"
	make -C cmake-build-dev -j profiler wrapper

