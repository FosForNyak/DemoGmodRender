# Linux build environment for the engine, the same toolchain as the CI Linux jobs:
# Ubuntu 24.04, GCC 13, Clang 18 (ASan/UBSan, libFuzzer), CMake, Ninja, vcpkg.
#
#   docker build -t gmdr-engine-linux -f engine/docker/linux.Dockerfile engine/docker
#   docker run --rm -v "$PWD:/src:ro" -v gmdr-vcpkg-cache:/root/.cache/vcpkg gmdr-engine-linux linux-gcc
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang llvm libclang-rt-18-dev cmake ninja-build git curl ca-certificates zip unzip tar pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*

# vcpkg itself; the ports come from the baseline pinned in engine/vcpkg-configuration.json.
RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg /opt/vcpkg && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
ENV VCPKG_ROOT=/opt/vcpkg

COPY run-preset.sh /usr/local/bin/run-preset
RUN chmod +x /usr/local/bin/run-preset
ENTRYPOINT ["run-preset"]
