
#include <unordered_set>

#include "JPEGindexer.h"
#include "kernels/HashMap.cuh"

#include "Timer.h"

using namespace std;

struct HuffmanTable {
	int num_codes_per_bit_length[16];
	int huffman_values[256];
	int huffman_keys[256];
	int codelengths[256];
};

struct QuantizationTable {
	int values[64];
};

struct TextureData {
	int width = 0;
	int height = 0;
	CUdeviceptr data = 0;
	CUdeviceptr huffmanTables = 0;
	CUdeviceptr quanttables = 0;
	CUdeviceptr mcuPositions = 0;
};

bool initialized = false;
CUdeviceptr cptr_toDecode = 0;
CUdeviceptr cptr_toDecodeCounter = 0;
CUdeviceptr cptr_decoded = 0;
CUdeviceptr cptr_TBSlots = 0;
CUdeviceptr cptr_TBSlotsCounter = 0;
CUdeviceptr cptr_cache_index = 0;
CUdeviceptr cptr_texture_pointer = 0;
CUdeviceptr cptr_dummyvar = 0;

vector<TextureData> texturesData;
HashMap* decodedMcuMap = nullptr;
HashMap* decodedMcuMap_tmp = nullptr;

constexpr int64_t NUM_DECODED_MCU_CAPACITY = 700'072;
constexpr int64_t BYTES_PER_DECODED_MCU = 16 * 16 * 4;
constexpr int64_t MAX_HEIGHT = 4096;
constexpr int64_t MAX_WIDTH = 4096;

void initJpegPipeline(){

	SplatEditor* editor = SplatEditor::instance;

	int largestNumberOfHuffmanCodes = 0;

	for (JPEGIndexer& indexer : editor->textureIndexer) {
		TextureData textureData;
		textureData.width = indexer.width;
		textureData.height = indexer.height;

		auto roundUp = [](int64_t value, int64_t n){
			return ((value + n - 1ll) / n) * n;
		};
		//int64_t alignedSize = roundUp(indexer.only_ac_data.size(), 384);
		textureData.data = CURuntime::alloc("textureData.data", indexer.only_ac_data.size() + 384);
		cuMemcpyHtoDAsync(textureData.data, indexer.only_ac_data.data(), indexer.only_ac_data.size() * sizeof(uint8_t), 0);

		textureData.mcuPositions = CURuntime::alloc("textureData.mcuPositions", indexer.mcu_index.size() * sizeof(uint32_t));
		cuMemcpyHtoDAsync(textureData.mcuPositions, indexer.mcu_index.data(), indexer.mcu_index.size() * sizeof(uint32_t), 0);


		vector<HuffmanTable> huffman_table_vector;
		for (const auto& class_entry : indexer.huffman_tables_components) {
			for (const auto& table_entry : class_entry.second) {
				HuffmanTable huff_table = {};
				int value_index = 0;
				for (const auto& code_value : table_entry.second) {
					const string& code = code_value.first;
					int value = code_value.second;

					int code_len = code.size();
					huff_table.num_codes_per_bit_length[code_len - 1] += 1;
					huff_table.huffman_keys[value_index] = std::stoi(code, nullptr, 2);
					huff_table.huffman_values[value_index] = value;
					value_index++;
				}

				largestNumberOfHuffmanCodes = max(largestNumberOfHuffmanCodes, int(table_entry.second.size()));
				// println("Number of Huffman Codes: {}", table_entry.second.size());

				// initialize list of code lengths for each huffman code.
				int codeIndex = 0; 
				for(int i = 0; i < 16; i++){
					int codeLength = i + 1;
					int numCodes = huff_table.num_codes_per_bit_length[i];
					for(int j = 0; j < numCodes; j++){
						huff_table.codelengths[codeIndex] = codeLength;
						codeIndex++;
					}
				}

				huffman_table_vector.push_back(huff_table);
			}
		}

		textureData.huffmanTables = CURuntime::alloc("textureData.huffmanTables", huffman_table_vector.size() * sizeof(HuffmanTable));
		cuMemcpyHtoDAsync(textureData.huffmanTables, huffman_table_vector.data(), huffman_table_vector.size() * sizeof(HuffmanTable), 0);

		vector<QuantizationTable> quant_table_vector;
		for (const auto& quant_entry : indexer.quantization_tables) {
			QuantizationTable quant_table = {};
			std::copy(quant_entry.second.begin(), quant_entry.second.end(), quant_table.values);
			quant_table_vector.push_back(quant_table);
		}
		textureData.quanttables = CURuntime::alloc("textureData.quanttables", quant_table_vector.size() * sizeof(QuantizationTable));
		cuMemcpyHtoDAsync(textureData.quanttables, quant_table_vector.data(), quant_table_vector.size() * sizeof(QuantizationTable), 0);

		texturesData.push_back(textureData);
	}

	println("Largest number of Huffman Codes: {}", largestNumberOfHuffmanCodes);

	cptr_texture_pointer = CURuntime::alloc("cptr_texture_pointer",   sizeof(TextureData) * texturesData.size());
	cptr_toDecode        = CURuntime::alloc("cptr_toDecode",          sizeof(uint32_t) * NUM_DECODED_MCU_CAPACITY);
	cptr_toDecodeCounter = CURuntime::alloc("cptr_toDecodeCounter",   sizeof(uint32_t));
	cptr_decoded         = CURuntime::alloc("cptr_decoded",           NUM_DECODED_MCU_CAPACITY * BYTES_PER_DECODED_MCU);
	cptr_TBSlots         = CURuntime::alloc("cptr_TBSlots",           sizeof(uint32_t) * NUM_DECODED_MCU_CAPACITY);
	cptr_TBSlotsCounter  = CURuntime::alloc("cptr_TBSlotsCounter",    sizeof(uint32_t));
	cptr_cache_index     = CURuntime::alloc("cptr_cache_index",       sizeof(uint32_t));
	cptr_dummyvar        = CURuntime::alloc("cptr_dummyvar",          sizeof(uint32_t));

	cuMemcpyHtoDAsync(cptr_texture_pointer, texturesData.data(), texturesData.size() * sizeof(TextureData), 0);
	editor->settings.max_mcus = ((MAX_WIDTH * MAX_HEIGHT) / 64);

	// It is allegedly better to have a capacity congruent to X ≡ 3 mod 4.
	// Supposedly leads to less clustering when limiting indices by "index % capacity".
	decodedMcuMap = new HashMap();
	decodedMcuMap->capacity = 1'000'003;
	decodedMcuMap->entries = (uint64_t*)CURuntime::alloc("decodedMcuMap->entries", 8 * decodedMcuMap->capacity);
	
	decodedMcuMap_tmp = new HashMap();
	decodedMcuMap_tmp->capacity = decodedMcuMap->capacity;
	decodedMcuMap_tmp->entries = (uint64_t*)CURuntime::alloc("decodedMcuMap_tmp->entries", 8 * decodedMcuMap->capacity);

	cuMemsetD8((CUdeviceptr)decodedMcuMap->entries, 0xff, 8 * decodedMcuMap->capacity);
	cuMemsetD8((CUdeviceptr)decodedMcuMap_tmp->entries, 0xff, 8 * decodedMcuMap->capacity);

	{
		uint32_t capacity = NUM_DECODED_MCU_CAPACITY;
		editor->prog_jpeg->launch("kernel_init_availableMcuSlots", {&cptr_TBSlots, &cptr_TBSlotsCounter, &capacity}, capacity,  0);

		cuMemsetD32(cptr_TBSlotsCounter, 0, 1);
	}

	initialized = true;
}

// Cuda-OpenGL interop
struct MappedTextures{
	vector<shared_ptr<GLTexture>> textures;
	vector<CUgraphicsResource> resources;
	vector<CUsurfObject> surfaces;
	vector<CUtexObject> cuTextures;
};

// Cuda-OpenGL interop
MappedTextures mapCudaGl(vector<shared_ptr<GLTexture>> textures){

	struct RegisteredTexture{
		GLTexture texture;
		CUgraphicsResource resource;
	};

	static unordered_map<int64_t, RegisteredTexture> registeredResources;

	// REGISTER TEXTURES
	for(shared_ptr<GLTexture> texture : textures){
		if(registeredResources.find(texture->ID) == registeredResources.end()){
			// does not exist - register
			println("register new texture");
			
			CUgraphicsResource resource;
			cuGraphicsGLRegisterImage(
				&resource, 
				texture->handle, 
				GL_TEXTURE_2D, 
				CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);
			

			RegisteredTexture registration;
			registration.texture = *texture;
			registration.resource = resource;

			registeredResources[texture->ID] = registration;
		}else if(registeredResources[texture->ID].texture.version != texture->version){

			println("texture size changed - re-register cuda graphics resource");

			// changed - register new
			CUgraphicsResource resource = registeredResources[texture->ID].resource;

			cuGraphicsUnregisterResource(resource);

			cuGraphicsGLRegisterImage(
				&resource, 
				texture->handle, 
				GL_TEXTURE_2D, 
				CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD);

			RegisteredTexture registration;
			registration.texture = *texture;
			registration.resource = resource;

			registeredResources[texture->ID] = registration;
		}
	}

	vector<CUgraphicsResource> resources;
	for(shared_ptr<GLTexture> texture : textures){
		CUgraphicsResource resource = registeredResources[texture->ID].resource;
		resources.push_back(resource);
	}

	cuGraphicsMapResources(resources.size(), resources.data(), ((CUstream)CU_STREAM_DEFAULT));

	MappedTextures mappings;

	for(int i = 0; i < resources.size(); i++){

		shared_ptr<GLTexture> texture = textures[i];
		CUgraphicsResource resource = resources[i];

		CUDA_RESOURCE_DESC res_desc = {};
		res_desc.resType = CUresourcetype::CU_RESOURCE_TYPE_ARRAY;
		cuGraphicsSubResourceGetMappedArray(&res_desc.res.array.hArray, resource, 0, 0);

		CUsurfObject surface;
		cuSurfObjectCreate(&surface, &res_desc);

		// CUDA_TEXTURE_DESC tex_desc = {};
		// tex_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
		// tex_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
		// tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
		

		// CUDA_RESOURCE_VIEW_DESC view_desc = {};
		// view_desc.format = CU_RES_VIEW_FORMAT_UINT_4X8;
		// view_desc.width = texture->width;
		// view_desc.height = texture->height;

		// CUtexObject cuTexture;
		// auto result = cuTexObjectCreate(&cuTexture, &res_desc, &tex_desc, nullptr);

		mappings.textures.push_back(texture);
		mappings.resources.push_back(resource);
		mappings.surfaces.push_back(surface);
		// mappings.cuTextures.push_back(cuTexture);
	}

	return mappings;
}

void unmapCudaGl(MappedTextures mappings){
	for(int i = 0; i < mappings.resources.size(); i++){
		cuSurfObjectDestroy(mappings.surfaces[i]);
		// cuTexObjectDestroy(mappings.cuTextures[i]);
	}

	cuGraphicsUnmapResources(mappings.resources.size(), mappings.resources.data(), ((CUstream)CU_STREAM_DEFAULT));
}

// Takes the G-Buffers that were rendered in OpenGL, decodes the needed JPEG MCUs, and then replaces G-Buffer stuff with textured pixels
void texturing_jpeg(vector<View> views, vector<shared_ptr<Framebuffer>> fbos){

	SplatEditor* editor = SplatEditor::instance;
	CudaModularProgram* prog_jpeg = editor->prog_jpeg;

	if(!initialized) initJpegPipeline();

	// we need to register and map 3 textures per view from OpenGL to CUDA
	// attachments[3 * i + 0]: target framebuffer
	// attachments[3 * i + 1]: First color attachment of G-Buffer
	// attachments[3 * i + 2]: Second color attachment of G-Buffer
	vector<shared_ptr<GLTexture>> attachments;
	for(int i = 0; i < views.size(); i++){
		attachments.push_back(views[i].framebuffer->colorAttachments[0]);
		attachments.push_back(fbos[i]->colorAttachments[0]);
		attachments.push_back(fbos[i]->colorAttachments[1]);
	}
	auto mappings = mapCudaGl(attachments);

	cuMemsetD32(cptr_cache_index, 0, 1);
	cuMemsetD32(cptr_toDecodeCounter, 0, 1);
	
	// A dummy kernel that takes the hit for the OpenGL-CUDA context switch.
	// Added so that we get proper timings for the mark kernel
	prog_jpeg->launch("kernel_dummy", {
		&cptr_dummyvar
	}, 1);

	// We start measuring timings after the dummy kernel
	auto custart = Timer::recordCudaTimestamp();

	constexpr bool SHOW_CACHING_VR = false;
	uint32_t decodeCounterLeft = 0;
	uint32_t decodeCounterRight = 0;

	// Identify MCUs that need decoding in each view
	for(int i = 0; i < views.size(); i++){
		auto view = views[i];
		auto fbo = fbos[i];

		uint32_t width = view.framebuffer->width;
		uint32_t height = view.framebuffer->height;
		int num_textures = editor->textureIndexer.size();

		// Figure out which MCUs need decoding
		prog_jpeg->launch("kernel_mark", {
			&width, 
			&height,
			&mappings.surfaces[3 * i + 1],
			&mappings.surfaces[3 * i + 2],
			&cptr_toDecode,
			&cptr_toDecodeCounter,
			&cptr_texture_pointer,
			&num_textures,
			decodedMcuMap
		}, width * height);

		// This is only to be able to visualize cached MCUs in VR.
		// For the right view, it helps us colorize cached MCUs in green, and new ones in red. 
		if(SHOW_CACHING_VR){
			cuCtxSynchronize();

			uint32_t toDecodeCounter = 0;
			cuMemcpyDtoH(&toDecodeCounter, cptr_toDecodeCounter, 4);

			if(i == 0) decodeCounterLeft = toDecodeCounter;
			if(i == 1) decodeCounterRight = toDecodeCounter;
		}

		// EVALUATION: Check how much MCUs are queued after each view (TODO: remove for production)
		// if(GLRenderer::frameCount % 10 == 0){
		// 	cuCtxSynchronize();

		// 	uint32_t toDecodeCounter = 0;
		// 	cuMemcpyDtoH(&toDecodeCounter, cptr_toDecodeCounter, 4);

		// 	if(views.size() == 2 && i == 0){
		// 		printf("queued(left): %6u, ", toDecodeCounter);
		// 	}else if(views.size() == 2 && i == 1){
		// 		printf("queued(left+right): %6u \n", toDecodeCounter);
		// 	}
		// }
	}

	// Decode queued MCUs
	// We do an indirect launch here:
	// - Launches a proxy decode kernel with one single thread
	// - That kernel then launches the actual decode kernel with the correct amount of threads.
	// - Done that way so that we do not have to memcpy the number of threads to the host. 
	prog_jpeg->launch("kernel_launch_decode", {
		&cptr_toDecodeCounter,
		&cptr_TBSlots, 
		&cptr_TBSlotsCounter,
		&cptr_toDecode,
		&cptr_decoded,
		&cptr_texture_pointer,
		decodedMcuMap,
	}, 1);

	// EVALUATION: set flag of MCUs for right eye in VR so that we can visualize them
	// TODO: Disable when not evaluating
	// if(SHOW_CACHING_VR && decodeCounterRight > decodeCounterLeft){ 
	// 	uint32_t first = decodeCounterLeft;
	// 	uint32_t count = decodeCounterRight - decodeCounterLeft;
	// 	prog_jpeg->launch("kernel_eval_showCacheVR", {
	// 		&toDecode,
	// 		decodedMcuMap,
	// 		&first,
	// 		&count
	// 	}, count);
	// }

	// Resolve each view, i.e., convert G-Buffer to textured rendering.
	for(int i = 0; i < views.size(); i++){
		auto view = views[i];
		auto fbo = fbos[i];

		uint32_t width = view.framebuffer->width;
		uint32_t height = view.framebuffer->height;
		int num_textures = editor->textureIndexer.size();
		
		// Launch threads in 1-dimensional arrangement
		// prog_jpeg->launch("kernel_resolve", {
		// 	&editor->launchArgs,
		// 	&i,
		// 	&width, 
		// 	&height,
		// 	&mappings.surfaces[3 * i + 1],
		// 	&mappings.surfaces[3 * i + 2],
		// 	&mappings.surfaces[3 * i + 0],
		// 	&editor->settings.showUVs,
		// 	&cptr_toDecode,
		// 	&cptr_decoded,
		// 	&cptr_texture_pointer,
		// 	&num_textures,
		// 	decodedMcuMap
		// }, width * height);


		{ // launch threads in 2D-arrangement with <tileSize>. Can help with locality, ensuring a warp processes nearby pixels
			auto custart = Timer::recordCudaTimestamp();

			int tileSize = 8;
			int blocksX = (width + tileSize - 1) / tileSize;
			int blocksY = (height + tileSize - 1) / tileSize;

			void* args[] = {
				&editor->launchArgs,
				&i,
				&width, 
				&height,
				&mappings.surfaces[3 * i + 0],
				&mappings.surfaces[3 * i + 1],
				&mappings.surfaces[3 * i + 2],
				&editor->settings.showUVs,
				&cptr_toDecode,
				&cptr_decoded,
				&cptr_texture_pointer,
				&num_textures,
				decodedMcuMap
			};

			string kernelName = "kernel_resolve";
			auto res_launch = cuLaunchKernel(prog_jpeg->kernels[kernelName],
				blocksX, blocksY, 1,
				tileSize, tileSize, 1,
				0, 0, args, nullptr);


			if (res_launch != CUDA_SUCCESS) {
				const char* str;
				cuGetErrorString(res_launch, &str);
				printf("error: %s \n", str);
				println("{} - {}", __FILE__, __LINE__);
				println("kernel: {}", kernelName);
			}

			Timer::recordDuration(kernelName, custart, Timer::recordCudaTimestamp());
		}
	}

	if(editor->settings.enableTextureBlockCaching){
		// Update the Cache:
		// - Remove MCUs that were not visible in current frame.
		// - Put slots of removed entries back into the list of available slots.
		
		// - Implemented by replicating all entries we preserve into a new hash map: decodedMcuMap_tmp
		// - Then memcpy result back in to main hash map

		cuMemsetD8((CUdeviceptr)decodedMcuMap_tmp->entries, 0xff, decodedMcuMap_tmp->capacity * 8);
		bool freezeCache = editor->settings.freezeCache;
		prog_jpeg->launch("kernel_update_cache", {
			decodedMcuMap, 
			decodedMcuMap_tmp, 
			&cptr_TBSlots,
			&cptr_TBSlotsCounter,
			&freezeCache
		}, decodedMcuMap->capacity);
		cuMemcpy((CUdeviceptr)decodedMcuMap->entries, (CUdeviceptr)decodedMcuMap_tmp->entries, decodedMcuMap_tmp->capacity * 8);
	}else{
		// Disable caching by fully clearing the MCU slot list and hash map at the end of each frame.
		// This let's us see how much slower the decode kernel becomes.
		cuMemsetD8((CUdeviceptr)decodedMcuMap->entries, 0xff, decodedMcuMap->capacity * 8);
		uint32_t capacity = NUM_DECODED_MCU_CAPACITY;
		editor->prog_jpeg->launch("kernel_init_availableMcuSlots", {&cptr_TBSlots, &cptr_TBSlotsCounter, &capacity}, capacity,  0);

		cuMemsetD32(cptr_TBSlotsCounter, 0, 1);
	}

	// if we cuMemcpyDtoHAsync the numMcusDecoded state infos here, things get very slow. 
	// So instead, let's copy it to the device state tracker, which is memcopied to host every frame anyway
	cuMemcpyDtoD(editor->cptr_state + offsetof(DeviceState, numMcusDecoded), cptr_toDecodeCounter, 4);

	auto cuend = Timer::recordCudaTimestamp();
	Timer::recordDuration("<jpeg pipeline>", custart, cuend);

	unmapCudaGl(mappings);
}

// Resolve G-Buffer with normal OpenGL shader.
// Only works if uncompressed textures are loaded, which is likely disabled.
// void texturing_gl(vector<View> views, vector<shared_ptr<Framebuffer>> fbos){

// 	SplatEditor* editor = SplatEditor::instance;

// 	auto tstart = Timer::recordGlTimestamp();

// 	static bool initialized = false;
// 	static Shader* shader = nullptr;
// 	static GLuint ssbo_textures;
// 	static GLuint64 ptr_ssbo_textures;

// 	if(!initialized){

// 		shader = new Shader(
// 			"./src/shaders/resolve.vs",
// 			"./src/shaders/resolve.fs"
// 		);

// 		glCreateBuffers(1, &ssbo_textures);
// 		glNamedBufferStorage(ssbo_textures, 65'000 * 8, nullptr, GL_DYNAMIC_STORAGE_BIT);
// 		glGetNamedBufferParameterui64vNV(ssbo_textures, GL_BUFFER_GPU_ADDRESS_NV, &ptr_ssbo_textures);
// 		glMakeNamedBufferResidentNV(ssbo_textures, GL_READ_ONLY);

// 		initialized = true;
// 	}

// 	vector<uint64_t> textureHandles;
// 	editor->scene.forEach<SNTriangles>([&](SNTriangles* node) {
// 		if (!node->visible) return;

// 		textureHandles.push_back(node->texture->handle_bindless);
// 	});

// 	glBindVertexArray(GLRenderer::dummyVao);
// 	glUseProgram(shader->program);

// 	glNamedBufferSubData(ssbo_textures, 0, textureHandles.size() * sizeof(uint64_t), textureHandles.data());
// 	glUniformui64NV(shader->uniformLocations["u_textures"], ptr_ssbo_textures);

// 	for(int i = 0; i < views.size(); i++){
// 		auto view = views[i];
// 		auto fbo = fbos[i];

// 		glBindFramebuffer(GL_FRAMEBUFFER, view.framebuffer->handle);

// 		glViewport(0, 0, view.framebuffer->width, view.framebuffer->height);
// 		glClearColor(0.3f, 0.3f, 0.0f, 1.0f);
// 		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
// 		glDisable(GL_CULL_FACE);
		
// 		GLuint64 tex_uv_handle = glGetTextureHandleARB(fbo->colorAttachments[0]->handle);
// 		GLuint64 tex_id_handle = glGetTextureHandleARB(fbo->colorAttachments[1]->handle);
// 		glMakeTextureHandleResidentARB(tex_uv_handle);
// 		glMakeTextureHandleResidentARB(tex_id_handle);

// 		glUniform1ui64ARB(shader->uniformLocations["tex_uv"], tex_uv_handle);
// 		glUniform1ui64ARB(shader->uniformLocations["tex_idmip"], tex_id_handle);

// 		glDrawArrays(GL_TRIANGLES, 0, 6);

// 		glMakeTextureHandleNonResidentARB(tex_uv_handle);
// 		glMakeTextureHandleNonResidentARB(tex_id_handle);
// 	}

// 	Timer::recordDuration("OpenGL resolve", tstart, Timer::recordGlTimestamp());
// }

struct GLNode{
	mat4 world;
	uint64_t ptr_position;
	uint64_t ptr_uv;
	int32_t textureIndex;
	int32_t width;
	int32_t height;
	int32_t padding0;
};

void SplatEditor::draw(Scene* scene, vector<View> views){

	static bool initialized = false;
	static Shader* shader = nullptr;
	static vector<shared_ptr<Framebuffer>> fbos;
	static GLuint ssbo_nodes;
	static GLuint64 ptr_nodes;

	if(!initialized){

		shader = new Shader(
			"./src/shaders/mesh.vs",
			"./src/shaders/mesh.fs"
		);

		glCreateBuffers(1, &ssbo_nodes);
		glNamedBufferStorage(ssbo_nodes, 10'000 * sizeof(GLNode), nullptr, GL_DYNAMIC_STORAGE_BIT);
		glGetNamedBufferParameterui64vNV(ssbo_nodes, GL_BUFFER_GPU_ADDRESS_NV, &ptr_nodes);
		glMakeNamedBufferResidentNV(ssbo_nodes, GL_READ_ONLY);
		
		initialized = true;
	}
	if(!shader) return;

	// Making sure that we have as many intermediate cached fbos as we have target views
	for(int i = fbos.size(); i < views.size(); i++){
		string label = format("intermediate fbo {}", i);
		shared_ptr<Framebuffer> fbo = Framebuffer::create(label);
		auto texture = GLRenderer::createTexture(fbo->width, fbo->height, GL_RGBA8, label);
		fbo->colorAttachments.push_back(texture);

		fbos.push_back(fbo);
	}

	auto tstart = Timer::recordGlTimestamp();

	// setup opengl renderer
	glDepthFunc(GL_GREATER);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	glDisable(GL_BLEND);

	// Create/update storage buffer of all visible scene nodes (ssbo_nodes) for single-draw bindless rendering
	vector<GLint> first;
	vector<GLint> count;
	vector<GLNode> glnodes;

	int numTrianglesTotal = 0;
	unordered_set<int> texIdSet;
	scene->forEach<SNTriangles>([&](SNTriangles* node) {
		if (!node->visible) return;

		GLNode glnode;
		glnode.world = node->transform_global;

		glnode.ptr_position = node->ptr_ssbo_position;
		glnode.ptr_uv = node->ptr_ssbo_uv;

		glnode.textureIndex = node->textureIndex;
		glnode.width = node->texture->width;
		glnode.height = node->texture->height;

		first.push_back(0);
		count.push_back(node->numTriangles * 3);
		glnodes.push_back(glnode);

		texIdSet.insert(node->textureIndex);
		numTrianglesTotal += node->numTriangles;
	});

	Runtime::debugValues["#nodes"] = format("{}", glnodes.size());
	Runtime::debugValues["#triangles"] = format(getSaneLocale(), "{:L}", numTrianglesTotal);
	Runtime::debugValues["#textures"] = format("{}", int(texIdSet.size()));
	
	glNamedBufferSubData(ssbo_nodes, 0, glnodes.size() * sizeof(GLNode), glnodes.data());
	glUniformui64NV(shader->uniformLocations["u_nodes"], ptr_nodes);
	glUniform1i(shader->uniformLocations["u_enableMipMapping"], settings.enableMipMapping);

	// Draw G-Buffers for each view using OpenGL
	for(int i = 0; i < views.size(); i++){
		View view = views[i];
		shared_ptr<Framebuffer> fbo = fbos[i];

		// setup render target
		glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle);
		GLenum attachments[2] = {
			GL_COLOR_ATTACHMENT0,
			GL_COLOR_ATTACHMENT1
		};
		glNamedFramebufferDrawBuffers(fbo->handle, 2, attachments);
		fbo->setSize(view.framebuffer->width, view.framebuffer->height);

		// clear render target
		glViewport(0, 0, fbo->width, fbo->height);
		glClearDepth(0.0);
		glClearColor(0.3f, 0.3f, 5.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// configure shader
		glBindVertexArray(GLRenderer::dummyVao);
		glUseProgram(shader->program);
		mat4 viewProj = view.proj * view.view;
		glUniformMatrix4fv(shader->uniformLocations["u_viewProj"], 1, GL_FALSE, (GLfloat*)&viewProj);

		// Draw all scene nodes 
		glMultiDrawArrays(GL_TRIANGLES, first.data(), count.data(), glnodes.size());
	}

	Timer::recordDuration("OpenGL geometry pass", tstart, Timer::recordGlTimestamp());

	// Next, convert G-Buffer to textured rendering
	if(settings.useJpegRendering){
		texturing_jpeg(views, fbos);
	}else{
		// texturing_gl(views, fbos);
	}
}

void SplatEditor::render(){

	if(GLRenderer::width * GLRenderer::height == 0){
		return;
	}

	GLRenderer::view.framebuffer->setSize(GLRenderer::width, GLRenderer::height);
	
	if(OpenVRHelper::instance()->isActive()){
		// RENDER VR
		auto ovr = OpenVRHelper::instance();

		dmat4 flip = glm::dmat4(
			1.0, 0.0, 0.0, 0.0,
			0.0, 0.0, 1.0, 0.0,
			0.0, -1.0, 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0
		);

		auto poseHMD = ovr->getHmdPose();
		auto poseLeft = ovr->getEyePose(vr::Hmd_Eye::Eye_Left);
		auto poseRight = ovr->getEyePose(vr::Hmd_Eye::Eye_Right);
		auto size = ovr->getRecommmendedRenderTargetSize();
		int width = size[0];
		int height = size[1];

		dmat4 rot(1.0);
		if(settings.rotBenchEnabled){
			// For the rotation benchmark: Rotate by 6 degree every frame.
			double angleDeg = (6 * settings.rotCounter) % 360;
			double angleRad = 3.1415 * angleDeg / 180.0;
			rot = glm::rotate(angleRad, dvec3{0.0, 0.0, 1.0});
		}

		view_left.framebuffer->setSize(size[0], size[1]);
		view_left.view = glm::inverse(rot * flip * poseHMD * poseLeft);
		view_left.proj = ovr->getProjection(vr::Hmd_Eye::Eye_Left, 0.02, 1'000.0);

		view_right.framebuffer->setSize(size[0], size[1]);
		view_right.view = glm::inverse(rot * flip * poseHMD * poseRight);
		view_right.proj = ovr->getProjection(vr::Hmd_Eye::Eye_Right, 0.02, 1'000.0);

		draw(&scene, {view_left, view_right});

		{ // BLIT LEFT TO DESKTOP
			auto source = view_left.framebuffer;
			auto target = GLRenderer::view.framebuffer;

			glBlitNamedFramebuffer(
				source->handle, target->handle,
				0, 0, source->width, source->height,
				0, 0, target->width / 2, target->height,
				GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

		{ // BLIT RIGHT TO DESKTOP
			auto source = view_right.framebuffer;
			auto target = GLRenderer::view.framebuffer;

			glBlitNamedFramebuffer(
				source->handle, target->handle,
				0, 0, source->width, source->height,
				target->width / 2, 0, target->width, target->height,
				GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}

		// SUBMIT TO OVR
		vr::VRTextureBounds_t bounds;
		bounds.uMin = 0.0f;
		bounds.vMin = 1.0f - float(height) / float(view_left.framebuffer->height);
		bounds.uMax = float(width) / float(view_left.framebuffer->width);
		bounds.vMax = 1.0f;

		ovr->submit(view_left.framebuffer->colorAttachments[0]->handle, vr::EVREye::Eye_Left, bounds);
		ovr->submit(view_right.framebuffer->colorAttachments[0]->handle, vr::EVREye::Eye_Right, bounds);
		ovr->postPresentHandoff();

	}else{
		// RENDER DESKTOP
		GLRenderer::view.proj =  GLRenderer::camera->proj;
		GLRenderer::view.view =  mat4(GLRenderer::camera->view);
		
		draw(&scene, {GLRenderer::view});
	}

	{ // DRAW GUI
		auto tstart = Timer::recordGlTimestamp();
		glBindFramebuffer(GL_FRAMEBUFFER, GLRenderer::view.framebuffer->handle);

		ImGui::NewFrame();
		ImGuizmo::BeginFrame();

		drawGUI();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		Timer::recordDuration("imgui", tstart, Timer::recordGlTimestamp());
	}
	
	mouse_prev = Runtime::mouseEvents;

	cuMemcpyDtoHAsync(h_state_pinned, cptr_state, sizeof(DeviceState), ((CUstream)CU_STREAM_DEFAULT));
	memcpy(&deviceState, h_state_pinned, sizeof(DeviceState)); // Doesn't really need to be in sync, this is just for debug info

	Runtime::mouseEvents.clear();
}