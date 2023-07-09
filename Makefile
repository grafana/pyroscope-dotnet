## AMD64
#$ docker build -t your-username/multiarch-example:manifest-amd64 --build-arg ARCH=amd64/ .
#$ docker push your-username/multiarch-example:manifest-amd64
#
## ARM32V7
#$ docker build -t your-username/multiarch-example:manifest-arm32v7 --build-arg ARCH=arm32v7/ .
#$ docker push your-username/multiarch-example:manifest-arm32v7
#
## ARM64V8
#$ docker build -t your-username/multiarch-example:manifest-arm64v8 --build-arg ARCH=arm64v8/ .
#$ docker push your-username/multiarch-example:manifest-arm64v8
#
#Now that we have built our images and pushed them, we are able to reference them all in a manifest list using the docker manifest command.
#
#$ docker manifest create \
#your-username/multiarch-example:manifest-latest \
#--amend your-username/multiarch-example:manifest-amd64 \
#--amend your-username/multiarch-example:manifest-arm32v7 \
#--amend your-username/multiarch-example:manifest-arm64v8
#
#Once the manifest list has been created, we can push it to Docker Hub.
#
#$ docker manifest push your-username/multiarch-example:manifest-latest

LIBC ?= glibc
ARCH ?= x86_64
RELEASE_VERSION ?= $(shell git describe --tags --always --dirty | grep -oP '(?<=v).*(?=-pyroscope)')
DOCKER_IMAGE ?= korniltsev/pyroscope-dotnet

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

drone:
	drone jsonnet -V BUILD_IMAGE_VERSION=$(BUILD_IMAGE_VERSION) --stream --format --source .drone/drone.jsonnet --target .drone/drone.yml
	drone lint .drone/drone.yml
	drone sign --save grafana/pyroscope-rs .drone/drone.yml