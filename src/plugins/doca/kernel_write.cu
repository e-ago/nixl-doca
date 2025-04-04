/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <doca_gpunetio_dev_buf.cuh>
#include <doca_gpunetio_dev_rdma.cuh>

#include "doca_backend.h"

__global__ void kernel_write(struct doca_gpu_dev_rdma *rdma_gpu, struct doca_gpu_buf_arr *lbarr, struct doca_gpu_buf_arr *rbarr, size_t size)
{
    doca_error_t result;
	struct doca_gpu_buf *lbuf;
    uintptr_t laddr;
	struct doca_gpu_buf *rbuf;
    uintptr_t raddr;
    const int connection_index = 0;
    uint32_t num_ops=0;

    doca_gpu_dev_buf_get_buf(lbarr, 0, &lbuf);
	doca_gpu_dev_buf_get_buf(rbarr, 0, &rbuf);
    doca_gpu_dev_buf_get_addr(lbuf, &laddr);
    doca_gpu_dev_buf_get_addr(rbuf, &raddr);

    printf(">>>>>>> CUDA rdma write kernel laddr %lx raddr %lx size %d\n", laddr, raddr, (int)size);

    //Each thread should send a different buffer
    result = doca_gpu_dev_rdma_write_strong(rdma_gpu, connection_index, rbuf, 0, lbuf, 0, size, 0, DOCA_GPU_RDMA_WRITE_FLAG_NONE);
    if (result != DOCA_SUCCESS)
        printf("Error %d doca_gpu_dev_rdma_write_strong\n", result);

    result = doca_gpu_dev_rdma_commit_strong(rdma_gpu, connection_index);
    if (result != DOCA_SUCCESS)
        printf("Error %d doca_gpu_dev_rdma_push\n", result);

    result = doca_gpu_dev_rdma_wait_all(rdma_gpu, &num_ops);
    if (result != DOCA_SUCCESS)
        printf("Error %d doca_gpu_dev_rdma_wait_all\n", result);

    printf("Rdma write kernel completed %d ops\n", num_ops);
}

extern "C" {

doca_error_t doca_kernel_write(cudaStream_t stream, struct doca_gpu_dev_rdma *rdma_gpu, struct doca_gpu_buf_arr *lbarr, struct doca_gpu_buf_arr *rbarr, size_t size)
{
    cudaError_t result = cudaSuccess;

    if (rdma_gpu == NULL || lbarr == NULL || rbarr == NULL || size == 0) {
        fprintf(stderr, "kernel_write_server invalid input values");
        return DOCA_ERROR_INVALID_VALUE;
    }

    /* Check no previous CUDA errors */
    result = cudaGetLastError();
    if (result != cudaSuccess) {
        fprintf(stderr, "[%s:%d] cuda failed with %s", __FILE__, __LINE__, cudaGetErrorString(result));
        return DOCA_ERROR_BAD_STATE;
    }

    kernel_write<<<1, 1, 0, stream>>>(rdma_gpu, lbarr, rbarr, size);
    result = cudaGetLastError();
    if (result != cudaSuccess) {
        fprintf(stderr, "[%s:%d] cuda failed with %s", __FILE__, __LINE__, cudaGetErrorString(result));
        return DOCA_ERROR_BAD_STATE;
    }

    return DOCA_SUCCESS;
}


} /* extern C */
    