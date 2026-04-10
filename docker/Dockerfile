# FluxCache Dockerfile — multi-stage build
# Stage 1: build FluxCache with vcpkg + minio-cpp
# Stage 2: minimal runtime image

# ---- Build stage ----
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    curl \
    pkg-config \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    librocksdb-dev \
    libyaml-cpp-dev \
    libfuse3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy project
COPY . /build/fluxcache

# Setup vcpkg
ARG VCPKG_ROOT=/build/vcpkg
RUN if [ ! -d /build/fluxcache/vcpkg ]; then \
      git clone --depth 1 https://github.com/microsoft/vcpkg.git /build/vcpkg && \
      /build/vcpkg/bootstrap-vcpkg.sh -disableMetrics; \
    fi
ENV VCPKG_ROOT=${VCPKG_ROOT}

# Build
RUN mkdir -p /build/fluxcache/build && \
    cd /build/fluxcache/build && \
    cmake .. \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DFLUXCACHE_ENABLE_FUSE=ON && \
    cmake --build . -j$(nproc)

# ---- Runtime stage ----
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libgrpc++1.51 \
    libprotobuf-lite23 \
    librocksdb7.9.3 \
    libyaml-cpp0.7 \
    libfuse3-3 \
    && rm -rf /var/lib/apt/lists/*

# Copy binaries
COPY --from=builder /build/fluxcache/build/fluxcache-master /usr/local/bin/
COPY --from=builder /build/fluxcache/build/fluxcache-worker /usr/local/bin/
COPY --from=builder /build/fluxcache/build/fluxcache-cli /usr/local/bin/

# Copy configs and scripts
COPY docker/config/ /etc/fluxcache/
COPY docker/run_e2e_test.sh /usr/local/bin/run_e2e_test.sh

RUN chmod +x /usr/local/bin/run_e2e_test.sh

# Default config
ENV FLUXCACHE_CONFIG=/etc/fluxcache/config.yaml

WORKDIR /etc/fluxcache
