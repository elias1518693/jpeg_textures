#pragma once

#include <string>
#include <unordered_map>
#include <print>

#include "unsuck.hpp"

#include "nvrtc.h"
#include <nvJitLink.h>
#include <cmath>
#include "cuda.h"
#include "Timer.h"

using std::string;

using namespace std;

#define NVJITLINK_SAFE_CALL(h,x)                                  \
do {                                                              \
   nvJitLinkResult result = x;                                    \
   if (result != NVJITLINK_SUCCESS) {                             \
      std::cerr << "\nerror: " #x " failed with error "           \
                << result << '\n';                                \
      size_t lsize;                                               \
      result = nvJitLinkGetErrorLogSize(h, &lsize);               \
      if (result == NVJITLINK_SUCCESS && lsize > 0) {             \
         char *log = (char*)malloc(lsize);                        \
         result = nvJitLinkGetErrorLog(h, log);                   \
         if (result == NVJITLINK_SUCCESS) {                       \
            std::cerr << "error: " << log << '\n';                \
            free(log);                                            \
         }                                                        \
      }                                                           \
      exit(1);                                                    \
   } else {                                                       \
      size_t lsize;                                               \
      result = nvJitLinkGetInfoLogSize(h, &lsize);                \
      if (result == NVJITLINK_SUCCESS && lsize > 0) {             \
         char *log = (char*)malloc(lsize);                        \
         result = nvJitLinkGetInfoLog(h, log);                    \
         if (result == NVJITLINK_SUCCESS) {                       \
            std::cerr << "info: " << log << '\n';                 \
            free(log);                                            \
         }                                                        \
      }                                                           \
      break;                                                      \
   }                                                              \
} while(0)


struct OptionalLaunchSettings{
	uint32_t gridsize = 0;
	uint32_t blocksize = 0;
	vector<void*> args;
	bool measureDuration = false;
	CUstream stream;
};

struct CudaModule{

	static void cu_checked(CUresult result){
		if(result != CUDA_SUCCESS){
			cout << "cuda error code: " << result << endl;
		}
	};

	string path = "";
	string name = "";
	bool compiled = false;
	bool success = false;
	
	size_t ptxSize = 0;
	char* ptx = nullptr;

	size_t ltoirSize = 0;
	char* ltoir = nullptr;

	CudaModule(string path, string name){
		this->path = path;
		this->name = name;
	}

	void compile(){
		auto tStart = now();

		cout << "================================================================================" << endl;
		cout << "=== COMPILING: " << fs::path(path).filename().string() << endl;
		cout << "================================================================================" << endl;

		success = false;

		string dir = fs::path(path).parent_path().string();

		string cuda_path = std::getenv("CUDA_PATH");
		string optInclude = std::format("-I {}", dir).c_str();
		string cuda_include = std::format("-I {}/include", cuda_path);
		string cudastd_include = std::format("-I {}/include/cuda/std", cuda_path);
		//string cudastd_detail_include = std::format("-I {}/include/cuda/std/detail/libcxx/include", cuda_path);

		CUdevice device;
		cuCtxGetDevice(&device);

		int major = 0;
		int minor = 0;
		cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
		cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);

		 string arch = format("--gpu-architecture=compute_{}{}", major, minor);
		//string arch = "--gpu-architecture=compute_86";

		nvrtcProgram prog;
		string source = readFile(path);
		nvrtcCreateProgram(&prog, source.c_str(), name.c_str(), 0, NULL, NULL);
		std::vector<const char*> opts = { 
			// "--gpu-architecture=compute_75",
			// "--gpu-architecture=compute_86",
			arch.c_str(),
			"--use_fast_math",
			"--extra-device-vectorization",
			"-lineinfo",
			cudastd_include.c_str(),
			cuda_include.c_str(),
			optInclude.c_str(),
			"-I ./",
			"-I ./include",
			"--relocatable-device-code=true",
			// "lcudadevrt",
			"-default-device",                   // assume __device__ if not specified
			"--dlink-time-opt",                  // link time optimization "-dlto", 
			// "--dopt=on",
			"--std=c++20",
			"--disable-warnings",
			"--split-compile=0",                 // compiler optimizations in parallel. 0 -> max available threads
			"--time=cuda_compile_time.txt",      // show compiler timings
		};


		for(auto opt : opts){
			cout << opt << endl;
		}
		cout << "====" << endl;

		nvrtcResult res = nvrtcCompileProgram(prog, opts.size(), opts.data());
		
		if (res != NVRTC_SUCCESS)
		{
			size_t logSize;
			nvrtcGetProgramLogSize(prog, &logSize);
			char* log = new char[logSize];
			nvrtcGetProgramLog(prog, log);
			//std::cerr << "Program Log: " <<  log << std::endl;
			println("Program Log: {}", log);

			delete[] log;

			if(res != NVRTC_SUCCESS && ltoir != nullptr){
				return;
			}else if(res != NVRTC_SUCCESS && ltoir == nullptr){
				println("failed gto compile {}. {}:{}", path, __FILE__, __LINE__);
				exit(123);
			}
		}

		nvrtcGetLTOIRSize(prog, &ltoirSize);
		ltoir = new char[ltoirSize];
		nvrtcGetLTOIR(prog, ltoir);

		cout << format("compiled ltoir. size: {} byte \n", ltoirSize);

		nvrtcDestroyProgram(&prog);

		compiled = true;
		success = true;

		printElapsedTime("compile " + name, tStart);
	}

};


struct CudaModularProgram{

	struct CudaModularProgramArgs{
		vector<string> modules;
		vector<string> kernels;
	};

	static void cu_checked(CUresult result){
		if(result != CUDA_SUCCESS){
			cout << "cuda error code: " << result << endl;
		}
	};

	vector<CudaModule*> modules;

	CUmodule mod;
	// CUfunction kernel = nullptr;
	void* cubin = nullptr;
	size_t cubinSize;

	vector<std::function<void(void)>> compileCallbacks;

	vector<string> kernelNames;
	unordered_map<string, CUfunction> kernels;

	unordered_map<string, CUevent> events_launch_start;
	unordered_map<string, CUevent> events_launch_end;
	// unordered_map<string, float> last_launch_duration;

	int MAX_LAUNCH_DURATIONS = 50;
	unordered_map<string, vector<float>> last_launch_durations;
	unordered_map<string, int> launches_per_frame;

	CudaModularProgram(){

	}

	CudaModularProgram(vector<string> modules){
		construct({.modules = modules,});
	}

	CudaModularProgram(CudaModularProgramArgs args){
		construct(args);
	}

	static CudaModularProgram* fromCubin(void* cubin, int64_t size){
		CudaModularProgram* program = new CudaModularProgram();

		program->cubin = cubin;
		program->cubinSize = size;

		cu_checked(cuModuleLoadData(&program->mod, cubin));

		{ // Retrieve Kernels
			uint32_t count = 0;
			cuModuleGetFunctionCount(&count, program->mod);

			vector<CUfunction> functions(count);
			cuModuleEnumerateFunctions(functions.data(), count, program->mod);

			program->kernelNames.clear();

			for(CUfunction function : functions){
				const char* name;

				cuFuncGetName(&name, function);

				string strName = name;

				// println("============================================");
				// println("KERNEL: \"{}\"", strName);
				// int value;

				// cuFuncGetAttribute(&value, CU_FUNC_ATTRIBUTE_NUM_REGS, function);
				// cuFuncGetAttribute(&value, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, function);

				program->kernelNames.push_back(strName);
				program->kernels[strName] = function;

				CUevent event_start;
				CUevent event_end;
				cuEventCreate(&event_start, CU_EVENT_DEFAULT);
				cuEventCreate(&event_end, CU_EVENT_DEFAULT);

				program->events_launch_start[strName] = event_start;
				program->events_launch_end[strName] = event_end;
			}
		}

		return program;
	}

	void construct(CudaModularProgramArgs args){
		vector<string> modulePaths = args.modules;
		// vector<string> kernelNames = args.kernels;

		// this->kernelNames = kernelNames;

		for(auto modulePath : modulePaths){

			string moduleName = fs::path(modulePath).filename().string();
			auto module = new CudaModule(modulePath, moduleName);

			module->compile();

			monitorFile(modulePath, [&, module]() {
				module->compile();
				link();
			});

			modules.push_back(module);
		}

		link();
	}

	void link(){

		cout << "================================================================================" << endl;
		cout << "=== LINKING" << endl;
		cout << "================================================================================" << endl;
		
		auto tStart = now();

		for(auto module : modules){
			if(!module->success){
				return;
			}
		}

		float walltime;
		constexpr uint32_t v_optimization_level = 1;
		constexpr uint32_t logSize = 8192;
		char info_log[logSize];
		char error_log[logSize];

		CUlinkState linkState;

		CUdevice cuDevice;
		cuDeviceGet(&cuDevice, 0);

		int major = 0;
		int minor = 0;
		cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, cuDevice);
		cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, cuDevice);

		 int arch = major * 10 + minor;
		//int arch = 86;
		string strArch = std::format("-arch=sm_{}", arch);

		const char *lopts[] = {
			"-dlto",      // link time optimization
			strArch.c_str(),
			"-time",
			// "lcudadevrt"
			"-verbose",
			"-O3",           // optimization level
			"-optimize-unused-variables",
			"-split-compile=0",
		};

		nvJitLinkHandle handle;
		nvJitLinkCreate(&handle, 2, lopts);

		for(auto module : modules){
			NVJITLINK_SAFE_CALL(handle, nvJitLinkAddData(handle, NVJITLINK_INPUT_LTOIR, (void *)module->ltoir, module->ltoirSize, module->name.c_str()));
		}
		NVJITLINK_SAFE_CALL(handle, nvJitLinkAddFile(handle, NVJITLINK_INPUT_ANY, CUDA_DEVRTLIB));

		NVJITLINK_SAFE_CALL(handle, nvJitLinkComplete(handle));
		NVJITLINK_SAFE_CALL(handle, nvJitLinkGetLinkedCubinSize(handle, &cubinSize));

		if(cubin){
			free(cubin);
			cubin = nullptr;
		}
		cubin = malloc(cubinSize);
		NVJITLINK_SAFE_CALL(handle, nvJitLinkGetLinkedCubin(handle, cubin));
		NVJITLINK_SAFE_CALL(handle, nvJitLinkDestroy(&handle));


		// static int cubinID = 0;
		// writeBinaryFile(format("./program_{}.cubin", cubinID), (uint8_t*)cubin, cubinSize);
		// cubinID++;

		cu_checked(cuModuleLoadData(&mod, cubin));

		{ // Retrieve Kernels
			uint32_t count = 0;
			cuModuleGetFunctionCount(&count, mod);

			vector<CUfunction> functions(count);
			cuModuleEnumerateFunctions(functions.data(), count, mod);

			kernelNames.clear();

			for(CUfunction function : functions){
				const char* name;

				cuFuncGetName(&name, function);

				string strName = name;

				// println("============================================");
				// println("KERNEL: \"{}\"", strName);
				// int value;

				// cuFuncGetAttribute(&value, CU_FUNC_ATTRIBUTE_NUM_REGS, function);
				// cuFuncGetAttribute(&value, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, function);

				kernelNames.push_back(strName);
				kernels[strName] = function;

				CUevent event_start;
				CUevent event_end;
				cuEventCreate(&event_start, CU_EVENT_DEFAULT);
				cuEventCreate(&event_end, CU_EVENT_DEFAULT);

				events_launch_start[strName] = event_start;
				events_launch_end[strName] = event_end;
			}
		}

		for(auto& callback : compileCallbacks){
			callback();
		}

		printElapsedTime("link duration: ", tStart);

	}

	void onCompile(std::function<void(void)> callback){
		compileCallbacks.push_back(callback);
	}

	void addLaunchDuration(string kernelName, float duration){
		last_launch_durations[kernelName].resize(MAX_LAUNCH_DURATIONS);

		last_launch_durations[kernelName][0] += duration;
		
		launches_per_frame[kernelName]++;
	}

	void launch(string kernelName, vector<void*> args, OptionalLaunchSettings launchArgs = {}){
		void** _args = &args[0];

		this->launch(kernelName, _args, launchArgs);
	}

	void launch(string kernelName, void** args, OptionalLaunchSettings launchArgs){
		auto custart = Timer::recordCudaTimestamp();

		auto res_launch = cuLaunchKernel(kernels[kernelName],
			launchArgs.gridsize, 1, 1,
			launchArgs.blocksize, 1, 1,
			0, launchArgs.stream, args, nullptr);


		if (res_launch != CUDA_SUCCESS) {
			const char* str;
			cuGetErrorString(res_launch, &str);
			printf("error: %s \n", str);
			cout << __FILE__ << " - " << __LINE__ << endl;
			println("kernel: {}", kernelName);
		}

		Timer::recordDuration(kernelName, custart, Timer::recordCudaTimestamp());
	}

	void launch(string kernelName, vector<void*> args, int count, CUstream stream = 0){
		if(count == 0) return;

		void** _args = &args[0];

		this->launch(kernelName, _args, count, stream);
	}

	void launch(string kernelName, void** args, int count, CUstream stream = 0){

		if (count == 0){
			// last_launch_durations[kernelName] += 0.0f;
			// addLaunchDuration(kernelName, duration);
			return;
		}

		CUevent event_start = events_launch_start[kernelName];
		CUevent event_end   = events_launch_end[kernelName];

		uint32_t blockSize = 256;
		uint32_t gridSize = (count + blockSize - 1) / blockSize;

		auto custart = Timer::recordCudaTimestamp();

		auto res_launch = cuLaunchKernel(kernels[kernelName],
			gridSize, 1, 1,
			blockSize, 1, 1,
			0, stream, args, nullptr);

		if (res_launch != CUDA_SUCCESS) {
			const char* str;
			cuGetErrorString(res_launch, &str);
			printf("error %d, %s \n", int(res_launch), str);
			println("{} - {}", __FILE__, __LINE__);
			println("failed to launch kernel \"{}\". Threadcount: {}, gridSize: {}", kernelName, count, gridSize);

			exit(42415);
		}

		Timer::recordDuration(kernelName, custart, Timer::recordCudaTimestamp());
	}

	void launchCooperative(string kernelName, vector<void*> args, OptionalLaunchSettings launchArgs = {}){
		void** _args = &args[0];

		this->launchCooperative(kernelName, _args, launchArgs);
	}

	void launchCooperative(string kernelName, void** args, OptionalLaunchSettings launchArgs = {}){

		auto custart = Timer::recordCudaTimestamp();

		CUdevice device;
		int numSMs;
		cuCtxGetDevice(&device);
		cuDeviceGetAttribute(&numSMs, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device);

		
		int blockSize = launchArgs.blocksize > 0 ? launchArgs.blocksize : 128;

		int numBlocks;
		CUresult resultcode = cuOccupancyMaxActiveBlocksPerMultiprocessor(&numBlocks, kernels[kernelName], blockSize, 0);
		numBlocks *= numSMs;
		
		//numGroups = 100;
		// make sure at least 10 workgroups are spawned)
		numBlocks = std::clamp(numBlocks, 10, 100'000);

		auto kernel = this->kernels[kernelName];
		auto res_launch = cuLaunchCooperativeKernel(kernel,
			numBlocks, 1, 1,
			blockSize, 1, 1,
			0, launchArgs.stream, args);

		if(res_launch != CUDA_SUCCESS){
			const char* str; 
			cuGetErrorString(res_launch, &str);
			printf("error: %s \n", str);
			println("{} - {}", __FILE__, __LINE__);
			println("launchCooperative(\"{}\")", kernelName);

			exit(64321);
		}

		Timer::recordDuration(kernelName, custart, Timer::recordCudaTimestamp());
	}

	void clearTimings(){
		for(auto& [key, value] : last_launch_durations){
			for(int i = value.size() - 1; i > 0; i--){
				value[i] = value[i - 1];
			}
			value[0] = 0.0f;
		}

		for(auto& [key, value] : launches_per_frame){
			value = 0;
		}
		
	}

	float getAvgTiming(string kernelName){
		if(last_launch_durations.find(kernelName) != last_launch_durations.end()){
			float sum = 0.0f;
			for(float value : last_launch_durations[kernelName]){
				sum += value;
			}

			float avg = sum / float(last_launch_durations[kernelName].size());

			return avg;
		}else{
			return 0.0f;
		}
	}

};