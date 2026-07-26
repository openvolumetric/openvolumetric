FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG VCPKG_COMMIT=40f3c709db80acf154ac4b17a1f83c564ebd022e

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        curl \
        ffmpeg \
        git \
        ninja-build \
        pkg-config \
        python3 \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && git -C /opt/vcpkg checkout "${VCPKG_COMMIT}" \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
    && chown -R ubuntu:ubuntu /opt/vcpkg

ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DISABLE_METRICS=1
ENV VCPKG_DEFAULT_BINARY_CACHE=/home/ubuntu/.cache/vcpkg/archives
ENV PATH=/opt/vcpkg:${PATH}

USER ubuntu
