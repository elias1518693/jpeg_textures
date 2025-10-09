#pragma once

#include <print>

#include "cuda.h"
#include "unsuck.hpp"

using std::println;

constexpr uint64_t DEFAULT_VIRTUAL_SIZE = 2'000'000'000;

// see https://developer.nvidia.com/blog/introducing-low-level-gpu-virtual-memory-management/
struct CudaVirtualMemory{

	uint64_t size = 0;
	uint64_t comitted = 0;
	uint64_t granularity = 0;
	CUdeviceptr cptr = 0;

	// Keeping track of allocated physical memory, so we can remap or free
	std::vector<CUmemGenericAllocationHandle> allocHandles;
	std::vector<uint64_t> allocHandleSizes;

	CudaVirtualMemory(){

	}

	~CudaVirtualMemory(){
		// TODO: free all memory

		// cuMemUnmap(ptr, size);
		// cuMemRelease(allocHandle);
		// cuMemAddressFree(ptr, size); 
	}

	void destroy(){

		// cuMemCreate          ->  cuMemRelease
		// cuMemMap             ->  cuMemUnmap
		// cuMemAddressReserve  ->  cuMemAddressFree 

		// TODO: cuMemUnmap ? 

		if(cptr == 0){
			println("WARNING: tried to destroy virtual memory that was already destroyed.");
			return;
		}
		
		for(auto handle : allocHandles){
			cuMemRelease(handle); 
		}
		allocHandles.clear();

		cuMemAddressFree(cptr, size);

		cptr = 0;
	}

	// allocate potentially large amounts of virtual memory
	static std::shared_ptr<CudaVirtualMemory> create(uint64_t virtualSize = DEFAULT_VIRTUAL_SIZE){

		CUdevice cuDevice;
		cuDeviceGet(&cuDevice, 0);
		
		CUmemAllocationProp prop = {};
		prop.type          = CU_MEM_ALLOCATION_TYPE_PINNED;
		prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		prop.location.id   = cuDevice;

		uint64_t granularity = 0;
		cuMemGetAllocationGranularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM);

		uint64_t padded_size = roundUp(virtualSize, granularity);

		// reserve lots of virtual memory
		CUdeviceptr cptr = 0;
		auto result = cuMemAddressReserve(&cptr, padded_size, 0, 0, 0);

		if(result != CUDA_SUCCESS){
			println("error {} while trying to reserve virtual memory.", int(result));
			exit(52457);
		}
		
		auto memory = std::make_shared<CudaVirtualMemory>();
		memory->size = padded_size;
		memory->granularity = granularity;
		memory->cptr = cptr;
		memory->comitted = 0;

		return memory;
	}

	// commits <size> physical memory. 
	void commit(uint64_t requested_size){

		int64_t padded_requested_size = roundUp(requested_size, granularity);
		int64_t required_additional_size = padded_requested_size - comitted;

		// Do we already have enough comitted memory?
		if(required_additional_size <= 0) return;

		// println("commit({:9}) - committing {:9} additional bytes. total: {:9} bytes", requested_size, required_additional_size, padded_requested_size);

		CUdevice cuDevice;
		cuDeviceGet(&cuDevice, 0);

		CUmemAllocationProp prop = {};
		prop.type          = CU_MEM_ALLOCATION_TYPE_PINNED;
		prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		prop.location.id   = cuDevice;

		// create a little bit of physical memory
		CUmemGenericAllocationHandle allocHandle;
		auto result = cuMemCreate(&allocHandle, required_additional_size, &prop, 0);
		
		if(result != CUDA_SUCCESS){
			println("error {} while trying to allocate physical memory.", int(result));
			exit(52458);
		}

		// and map the physical memory
		cuMemMap(cptr + comitted, required_additional_size, 0, allocHandle, 0); 

		// make the new memory accessible
		CUmemAccessDesc accessDesc = {};
		accessDesc.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		accessDesc.location.id = cuDevice;
		accessDesc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
		cuMemSetAccess(cptr + comitted, required_additional_size, &accessDesc, 1);

		comitted += required_additional_size;
		allocHandles.push_back(allocHandle);
		allocHandleSizes.push_back(required_additional_size);
	}

};

