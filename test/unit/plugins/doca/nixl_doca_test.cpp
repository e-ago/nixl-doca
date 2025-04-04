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

#include "common.h"

static void checkCudaError(cudaError_t result, const char *message) {
	if (result != cudaSuccess) {
		std::cerr << message << " (Error code: " << result << " - "
				   << cudaGetErrorString(result) << ")" << std::endl;
		exit(EXIT_FAILURE);
	}
}

std::vector<std::string> split(const std::string& str, const char *delimiter) {
	std::vector<std::string> tokens;
	std::string token;
	size_t start = 0, end = 0;

	while ((end = str.find(delimiter, start)) != std::string::npos) {
		end += delimiter_stride;
		token = str.substr(start, end - start);
		tokens.push_back(token);
		start = end + 1;
	}
	// Add the last token
	token = str.substr(start);
	tokens.push_back(token);
	return tokens;
}

int main(int argc, char *argv[])
{
	// nixl_status_t           ret = NIXL_SUCCESS;
	
	std::string             role;
	// int                     status = 0;
	// int                     i;
	// int                     fd[NUM_TRANSFERS];
	void                    *addr_initiator[NUM_TRANSFERS];
	void                    *addr_target[NUM_TRANSFERS];
	nixlAgentConfig         cfg(true);
	nixl_b_params_t         params;
	nixlBlobDesc            buf_initiator[NUM_TRANSFERS];
	nixlBlobDesc            buf_target[NUM_TRANSFERS];
	// nixlBlobDesc            ftrans[NUM_TRANSFERS];
	nixlBackendH            *doca_initiator;
	nixlBackendH            *doca_target;
	nixlBlobDesc			desc;
	nixlXferReqH            *treq;
	std::string             name_plugin = "DOCA";
	nixl_opt_args_t extra_params_initiator;
	nixl_opt_args_t extra_params_target;
	nixl_reg_dlist_t dram_for_doca_initiator(DRAM_SEG);
	nixl_reg_dlist_t dram_for_doca_target(DRAM_SEG);
	cudaStream_t stream;
	nixl_status_t status;
	std::string name;
	nixl_blob_t metadata_target;
	nixl_blob_t metadata_initiator;
	nixlSerDes *serdes_target    = new nixlSerDes();
	nixlSerDes *serdes_initiator = new nixlSerDes();
	nixl_blob_t             remote_desc;

	params["network_devices"] = "mlx5_0";
	params["gpu_devices"] = "8A:00.0";

	checkCudaError(cudaSetDevice(0), "Failed to set device");
	cudaFree(0);
	checkCudaError(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Failed to create CUDA stream");

	std::cout << "Starting Agent for DOCA Test\n";

	/* ********************************* Initiator ********************************* */

	nixlAgent agent_initiator("doca_initiator", cfg);
	agent_initiator.createBackend(name_plugin, params, doca_initiator);
	if (doca_initiator == nullptr) {
		std::cerr <<"Error creating a new backend\n";
		exit(-1);
	}
	extra_params_initiator.backends.push_back(doca_initiator);

	std::cout << "DOCA Backend initiator created\n";

	for (int i = 0; i < NUM_TRANSFERS; i++) {
		checkCudaError(cudaMalloc(&addr_initiator[i], SIZE), "Failed to allocate CUDA buffer 0");
		checkCudaError(cudaMemset(addr_initiator[i], 0, SIZE), "Failed to memset CUDA buffer 0");
		buf_initiator[i].addr  = (uintptr_t)(addr_initiator[i]);
		buf_initiator[i].len   = SIZE;
		buf_initiator[i].devId = 0;
		dram_for_doca_initiator.addDesc(buf_initiator[i]);
		std::cout << "GPU alloc buffer " << i << "\n";
	}
	agent_initiator.registerMem(dram_for_doca_initiator, &extra_params_initiator);
	std::cout << "DOCA initiator registerMem local\n";

	/* ********************************* Target ********************************* */

	nixlAgent agent_target("doca_target", cfg);
	agent_target.createBackend(name_plugin, params, doca_target);
	if (doca_target == nullptr) {
		std::cerr <<"Error creating a new backend\n";
		exit(-1);
	}
	extra_params_target.backends.push_back(doca_target);

	std::cout << "DOCA Backend target created\n";

	/* As this is a unit test single peer, fake the remote memory with different local memory */
	for (int i = 0; i < NUM_TRANSFERS; i++) {
		checkCudaError(cudaMalloc(&addr_target[i], SIZE), "Failed to allocate CUDA buffer 0");
		checkCudaError(cudaMemset(addr_target[i], 0, SIZE), "Failed to memset CUDA buffer 0");
		buf_target[i].addr  = (uintptr_t)(addr_target[i]);
		buf_target[i].len   = SIZE;
		buf_target[i].devId = 0;
		dram_for_doca_target.addDesc(buf_target[i]);
		std::cout << "GPU alloc buffer " << i << "\n";
	}
	std::cout << "DOCA registerMem remote\n";
	agent_target.registerMem(dram_for_doca_target, &extra_params_target);

	/* ********************************* Single process handshake ********************************* */

	agent_target.getLocalMD(metadata_target);
	assert(serdes_target->addStr("AgentMD", metadata_target) == NIXL_SUCCESS);
	assert(dram_for_doca_target.trim().serialize(serdes_target) == NIXL_SUCCESS);
	std::string str_desc = serdes_target->exportStr();
	/* Here we should have send/recv */
	serdes_initiator->importStr(str_desc);
	metadata_initiator = serdes_initiator->getStr("AgentMD");
	assert (metadata_initiator != "");
	agent_initiator.loadRemoteMD(metadata_initiator, name);

	/* ********************************* Create initiator -> target Xfer req ********************************* */

	nixl_xfer_dlist_t dram_initiator_doca = dram_for_doca_initiator.trim();
	nixl_xfer_dlist_t dram_target_doca(serdes_initiator);
	extra_params_initiator.customParam = (uintptr_t)stream;

	status = agent_initiator.createXferReq(NIXL_WRITE, dram_initiator_doca, dram_target_doca,
		"doca_target", treq, &extra_params_initiator);
	if (status != NIXL_SUCCESS) {
		std::cerr << "Error creating transfer request\n";
		exit(-1);
	}

	std::cout << "Launch simple kernel on stream\n";
	launch_simple_kernel(stream, buf_initiator[0].addr, buf_initiator[0].len);
	std::cout << "Post the request with DOCA backend\n ";
	status = agent_initiator.postXferReq(treq);
	std::cout << "Waiting for completion\n";

	while(1);
	while (status != NIXL_SUCCESS) {
		status = agent_initiator.getXferStatus(treq);
		assert(status >= 0);
	}
	std::cout <<" Completed writing data using DOCA backend\n";
	agent_initiator.releaseXferReq(treq);


	std::cout << "Memory cleanup.. \n";
	agent_initiator.deregisterMem(dram_for_doca_initiator, &extra_params_initiator);
	agent_target.deregisterMem(dram_for_doca_target, &extra_params_target);
	
	std::cout << "Closing.. \n";

	cudaStreamDestroy(stream);

	/** Argument Parsing */
	// if (argc < 2) {
	//     std::cout <<"Enter the required arguments\n" << std::endl;
	//     std::cout <<"Directory path " << std::endl;
	//     exit(-1);
	// }

		return 0;
}
