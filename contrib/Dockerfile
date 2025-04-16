# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ARG BASE_IMAGE="nvcr.io/nvidia/cuda-dl-base"
ARG BASE_IMAGE_TAG="25.03-cuda12.8-devel-ubuntu24.04"

FROM ${BASE_IMAGE}:${BASE_IMAGE_TAG}

ARG DEFAULT_PYTHON_VERSION="3.12"

RUN apt-get update -y && \
    DEBIAN_FRONTEND=noninteractive apt-get -y install \
    ninja-build \
    pybind11-dev \
    cmake

COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /bin/

WORKDIR /workspace/nixl

# LD_LIBRARY_PATH is needed for auditwheel to find libcuda.so.1
# Set incorrectly to `compat/lib` in cuda-dl-base image
ENV LD_LIBRARY_PATH=/usr/local/cuda/compat/lib.real:$LD_LIBRARY_PATH

ENV VIRTUAL_ENV=/workspace/nixl/.venv
RUN uv venv $VIRTUAL_ENV --python $DEFAULT_PYTHON_VERSION && \
    # pybind11 pip install needed for ubuntu 22.04
    uv pip install --upgrade meson pybind11 patchelf

COPY . /workspace/nixl

ENV DOCA_REPO_LINK=https://linux.mellanox.com/public/repo/doca/2.10.0/ubuntu24.04/x86_64

RUN LOCAL_GPG_KEY_PATH="/usr/share/keyrings/mellanox-archive-keyring.gpg" \
    && curl -fsSL ${DOCA_REPO_LINK}/GPG-KEY-Mellanox.pub | gpg --dearmor | tee ${LOCAL_GPG_KEY_PATH} \
    && echo "deb [signed-by=${LOCAL_GPG_KEY_PATH}] ${DOCA_REPO_LINK} ./" | tee /etc/apt/sources.list.d/mellanox.list \
    && apt update -y \
    && apt install -y --no-install-recommends mlnx-dpdk mlnx-dpdk-dev \
    && apt install -y --no-install-recommends doca-sdk-argp doca-sdk-common doca-sdk-compress doca-sdk-dma doca-sdk-dpdk-bridge doca-sdk-eth doca-sdk-flow doca-sdk-rdma doca-all doca-sdk-gpunetio libdoca-sdk-gpunetio-dev rdma-core

ENV PKG_CONFIG_PATH=/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig:/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig:PKG_CONFIG_PATH
RUN rm -rf build && \
    mkdir build && \
    uv run meson setup build/ --prefix=/usr/local/nixl && \
    cd build && \
    ninja && \
    ninja install

ENV NIXL_PLUGIN_DIR=/usr/local/nixl/lib/x86_64-linux-gnu/plugins
RUN echo "/usr/local/nixl/lib/x86_64-linux-gnu" > /etc/ld.so.conf.d/nixl.conf && \
    echo "/usr/local/nixl/lib/x86_64-linux-gnu/plugins" >> /etc/ld.so.conf.d/nixl.conf && \
    ldconfig

# Create the wheel
ARG WHL_PYTHON_VERSIONS="3.12"
ARG WHL_PLATFORM="manylinux_2_39_x86_64"
RUN IFS=',' read -ra PYTHON_VERSIONS <<< "$WHL_PYTHON_VERSIONS" && \
    for PYTHON_VERSION in "${PYTHON_VERSIONS[@]}"; do \
        uv build --wheel --out-dir /tmp/dist --python $PYTHON_VERSION; \
    done
RUN uv pip install auditwheel && \
    uv run auditwheel repair /tmp/dist/nixl-*cp31*.whl --plat $WHL_PLATFORM --wheel-dir /workspace/nixl/dist

RUN uv pip install dist/nixl-*cp${DEFAULT_PYTHON_VERSION//./}*.whl