local build_image = import '../util/build_image.jsonnet';
local pipelines = import '../util/pipelines.jsonnet';

local make(cmd) = {
    trigger: { event: ['pull_request'] },
    steps: [
      {
        name: cmd,
        image: build_image.linux,
        commands: [cmd],
      },
    ],
  };

[
  pipelines.linux_amd64('docker glibc amd  ') + make('LIBC=glibc ARCH=amd   make docker/build'),
  pipelines.linux_amd64('docker musl  amd  ') + make('LIBC=musl  ARCH=amd   make docker/build'),
  pipelines.linux_arm64('docker glibc arm64') + make('LIBC=glibc ARCH=arm64 make docker/build'),
  pipelines.linux_arm64('docker musl  arm64') + make('LIBC=musl  ARCH=arm64 make docker/build'),
]
