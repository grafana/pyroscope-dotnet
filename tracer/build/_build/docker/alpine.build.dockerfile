# syntax=docker/dockerfile:1.6@sha256:ac85f380a63b13dfcefa89046420e1781752bab202122f8f50032edf31be0021

FROM alpine:3.14@sha256:0f2d5c38dd7a4f4f733e688e3a6733cb5ab1ac6e3cb4603a5dd564e5bfb80eed as base

RUN apk update \
        && apk upgrade \
        && apk add --no-cache \
        cmake \
        git \
        make \
        alpine-sdk \
        autoconf \
        libtool \
        automake \
        xz-dev \
        build-base \
        python3 \
        linux-headers \
        libexecinfo-dev

## snapshot 

FROM base AS build-llvm-clang

COPY alpine_build_patch_llvm_clang.sh alpine_build_patch_llvm_clang.sh

# build and install llvm/clang
RUN git clone --depth 1 --branch llvmorg-16.0.6 https://github.com/llvm/llvm-project.git && \
    # download patches to apply
    chmod +x alpine_build_patch_llvm_clang.sh && \
    ./alpine_build_patch_llvm_clang.sh && \
    \
    # setup build folder
    cd llvm-project && \
    mkdir build && \
    cd build && \
    \
    # build llvm/clang
    cmake  -DCOMPILER_RT_BUILD_GWP_ASAN=OFF -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;compiler-rt;lld" -DCMAKE_BUILD_TYPE=Release -DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=1 -DENABLE_LINKER_BUILD_ID=ON -DCLANG_VENDOR=Alpine -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-alpine-linux-musl -DLLVM_HOST_TRIPLE=x86_64-alpine-linux-musl  -G "Unix Makefiles" ../llvm && \
    make -j$(nproc)

FROM base as final
RUN --mount=target=/llvm-project,from=build-llvm-clang,source=llvm-project,rw cd /llvm-project/build && make install