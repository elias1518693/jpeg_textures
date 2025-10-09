
#include <cstdio>
#include <format>
#include <print>
#include <filesystem>
#include <string>
#include <queue>
#include <vector>
#include <algorithm>
#include <execution>

#include "unsuck.hpp"

#include "cuda.h"
#include "cuda_runtime.h"
#include "CudaModularProgram.h"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "Runtime.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "json/json.hpp"
#include "stb/stb_image_resize2.h"
#include "SplatEditor.h"

#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tinygltf/tiny_gltf.h"
#include "jpg/turbojpeg.h"

using namespace std;

void initCuda() {
	cuInit(0);

	CUcontext context;
	cuDeviceGet(&CURuntime::device, 0);
	cuCtxCreate(&context, 0, CURuntime::device);
}

bool resize_jpeg_buffer_turbo(const std::vector<uint8_t>& jpeg_input,
	int new_width,
	int new_height,
	std::vector<uint8_t>& jpeg_output,
	int quality = 80
){
	tjhandle tjInstance = tjInitDecompress();
	if (!tjInstance) {
		std::cerr << "TurboJPEG init decompress error: " << tjGetErrorStr() << std::endl;
		return false;
	}

	int width, height, jpegSubsamp, jpegColorspace;
	if (tjDecompressHeader3(tjInstance, jpeg_input.data(), jpeg_input.size(),
		&width, &height, &jpegSubsamp, &jpegColorspace) != 0)
	{
		std::cerr << "TurboJPEG decompress header error: " << tjGetErrorStr() << std::endl;
		tjDestroy(tjInstance);
		return false;
	}

	std::vector<uint8_t> decodedRGB(width * height * 3);
	if (tjDecompress2(tjInstance, jpeg_input.data(), jpeg_input.size(),
		decodedRGB.data(), width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) != 0)
	{
		std::cerr << "TurboJPEG decompress error: " << tjGetErrorStr() << std::endl;
		tjDestroy(tjInstance);
		return false;
	}

	tjDestroy(tjInstance);

	std::vector<uint8_t> resizedRGB(new_width * new_height * 3);

		stbir_resize_uint8_linear(
		decodedRGB.data(), width, height, 0,
		resizedRGB.data(), new_width, new_height, 0,
		STBIR_RGB
	);


	tjhandle tjComp = tjInitCompress();
	if (!tjComp) {
		std::cerr << "TurboJPEG init compress error: " << tjGetErrorStr() << std::endl;
		return false;
	}

	unsigned char* jpegBuf = nullptr;
	unsigned long jpegSize = 0;

	if (tjCompress2(tjComp, resizedRGB.data(), new_width, 0, new_height, TJPF_RGB,
		&jpegBuf, &jpegSize, TJSAMP_420, quality, TJFLAG_FASTDCT) != 0)
	{
		std::cerr << "TurboJPEG compress error: " << tjGetErrorStr() << std::endl;
		tjDestroy(tjComp);
		return false;
	}
	jpeg_output.assign(jpegBuf, jpegBuf + jpegSize);
	tjFree(jpegBuf);
	tjDestroy(tjComp);
	return true;
}

shared_ptr<SceneNode> loadModel(string file, int quality){

	println("loading model {}", file);

	SplatEditor* editor = SplatEditor::instance;
	Scene& scene = editor->scene;

	std::map<int, int>textureIdMap;
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, file);
	if (!warn.empty()) {
		std::cout << "Warning: " << warn << std::endl;
	}
	if (!err.empty()) {
		std::cout << "Error: " << err << std::endl;
	}
	if (!ret) {
		std::cout << "Failed to load GLB file!" << std::endl;
	}

	// Extract mesh data from glb
	// To avoid threading issues, we first prepare all the data in a multi-threaded fashion, then send them to the GPU with the main thread afterwards
	struct MeshPrepData{
		string name;
		vector<vec3> positions;
		vector<vec2> uvs;

		int textureIndex = 0;
		int textureWidth = 0;
		int textureHeight = 0;
		vector<uint8_t> decodedTexture;
		vector<JPEGIndexer> indexerLevels;
	};

	vector<MeshPrepData> preppedMeshes;

	atomic_int32_t idx = 0;
	mutex mtx;
	double t_start = now();
	println("parse model data");
	for (const auto& mesh : model.meshes) {

		// Decode and prep primitives in parallel
		for_each(std::execution::par, mesh.primitives.begin(), mesh.primitives.end(), [&](const auto& primitive){

			vector<vec3> positions;
			vector<vec2> uvs;
			if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
				println("Only triangle mesh supported!");
				return;
			}

			// Position Attribute
			auto itPos = primitive.attributes.find("POSITION");
			if (itPos == primitive.attributes.end()) {
				println("Missing POSITION attribute!");
				return;
			}
			const tinygltf::Accessor& posAccessor = model.accessors[itPos->second];
			const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
			const tinygltf::Buffer& posBuffer = model.buffers[posView.buffer];

			const float* posData = reinterpret_cast<const float*>(&posBuffer.data[posView.byteOffset + posAccessor.byteOffset]);
			for (size_t i = 0; i < posAccessor.count; i++) {
				positions.emplace_back(posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]);
			}

			// UV Attribute (Optional)
			auto itUV = primitive.attributes.find("TEXCOORD_0");
			if (itUV != primitive.attributes.end()) {
				const tinygltf::Accessor& uvAccessor = model.accessors[itUV->second];
				const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
				const tinygltf::Buffer& uvBuffer = model.buffers[uvView.buffer];

				const float* uvData = reinterpret_cast<const float*>(&uvBuffer.data[uvView.byteOffset + uvAccessor.byteOffset]);
				for (size_t i = 0; i < uvAccessor.count; i++) {
					uvs.emplace_back(uvData[i * 2], uvData[i * 2 + 1]);
				}
			}

			// Handle indices (if present)
			if (primitive.indices >= 0) {
				std::vector<glm::vec3> indexedPositions;
				std::vector<glm::vec2> indexedUVs;

				const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
				const tinygltf::Buffer& indexBuffer = model.buffers[indexView.buffer];

				const unsigned char* indexData = &indexBuffer.data[indexView.byteOffset + indexAccessor.byteOffset];

				for (size_t i = 0; i < indexAccessor.count; i++) {
					uint32_t index = 0;
					if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
						index = indexData[i];
					} else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
						index = reinterpret_cast<const uint16_t*>(indexData)[i];
					} else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
						index = reinterpret_cast<const uint32_t*>(indexData)[i];
					}

					indexedPositions.push_back(positions[index]);
					if (!uvs.empty()) {
						indexedUVs.push_back(uvs[index]);
					}
				}

				// Replace original arrays with indexed versions
				positions = indexedPositions;
				uvs = indexedUVs;
			}
			uint8_t textureIndex = 0;
			vector<uint8_t> gltfTextureData;
			int textureWidth = 0, textureHeight = 0, textureChannels = 0;

			if (primitive.material >= 0 && primitive.material < model.materials.size()) {
				const tinygltf::Material& material = model.materials[primitive.material];

				if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
					textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;

					// Get the texture reference
					if (textureIndex < model.textures.size()) {
						const tinygltf::Texture& texture = model.textures[textureIndex];

						// Get the image reference
						if (texture.source >= 0 && texture.source < model.images.size()) {
							const tinygltf::Image& image = model.images[texture.source];

							if (image.bufferView >= 0 && image.bufferView < model.bufferViews.size()) {
								// Get the bufferView that contains the image data
								const tinygltf::BufferView& bufferView = model.bufferViews[image.bufferView];
								const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

								// Extract binary image data
								gltfTextureData = std::vector<uint8_t>(
									buffer.data.begin() + bufferView.byteOffset,
									buffer.data.begin() + bufferView.byteOffset + bufferView.byteLength
								);

								size_t textureSize = bufferView.byteLength;
								// println("Extracted embedded texture {} Size: {} bytes", image.mimeType, textureSize);
								// Store texture metadata
								textureWidth = image.width;
								textureHeight = image.height;
								textureChannels = image.component;
							}else {
								std::cerr << "Invalid bufferView for texture!" << std::endl;
							}
						}
					}
				}
			}

			vector<uint8_t> decodedTexture;
			{
				int width, height, channels = 0;
				uint8_t* imageData = stbi_load_from_memory(gltfTextureData.data(), gltfTextureData.size(), &width, &height, &channels, 0);

				decodedTexture = vector<uint8_t>(width * height * 4);
				for(int i = 0; i < width * height; i++){
					decodedTexture[4 * i + 0] = imageData[3 * i + 0];
					decodedTexture[4 * i + 1] = imageData[3 * i + 1];
					decodedTexture[4 * i + 2] = imageData[3 * i + 2];
					decodedTexture[4 * i + 3] = 255;
				}

				stbi_image_free(imageData);
			}

			vector<JPEGIndexer> jpegLevels;
			jpegLevels.push_back(JPEGIndexer(gltfTextureData));

			int height;
			int n;
			int width;
			for (int i = 1; i < 8; i++) {
				vector<uint8_t>test_jpeg;

				if (textureWidth / pow(2, i) < 4 || textureHeight / pow(2, i) < 4) {
					resize_jpeg_buffer_turbo(gltfTextureData, 4, 4, test_jpeg, 10);
				} else {
					resize_jpeg_buffer_turbo(gltfTextureData, textureWidth / pow(2,i), textureHeight / pow(2,i), test_jpeg, quality);
				}

				JPEGIndexer indexer = JPEGIndexer(test_jpeg);
				indexer.mipMapLevel = i;
				jpegLevels.push_back(indexer);
			}

			string name = format("triangles: {}", positions.size() / 3);

			MeshPrepData preppedMesh;
			preppedMesh.name = name;
			preppedMesh.positions = positions;
			preppedMesh.uvs = uvs;
			preppedMesh.textureIndex = textureIndex;
			preppedMesh.decodedTexture = decodedTexture;
			preppedMesh.textureWidth = textureWidth;
			preppedMesh.textureHeight = textureHeight;
			preppedMesh.indexerLevels = jpegLevels;

			lock_guard<mutex> lock(mtx);
			println("loaded mesh[{}]", preppedMeshes.size());
			preppedMeshes.push_back(preppedMesh);
		});
	}

	printElapsedTime("Time to prep meshes", t_start);

	// Now copy mesh data to GPU in main thread
	println("Send model data to GPU");
	shared_ptr<SceneNode> container = make_shared<SceneNode>("container");
	for(int i = 0; i < preppedMeshes.size(); i++){

		MeshPrepData& prepped = preppedMeshes[i];

		shared_ptr<SNTriangles> node = make_shared<SNTriangles>(prepped.name);

		node->set(prepped.positions, prepped.uvs);
		// node->transform = mat4(
		// 	1.000,  0.000, 0.000, 0.000,
		// 	0.000,  0.000, 1.000, 0.000,
		// 	0.000, -1.000, 0.000, 0.000,
		// 	0.000,  0.000, 0.000, 1.000);

		auto& textureData = prepped.decodedTexture;
		int textureIndex = prepped.textureIndex;
		int textureWidth = prepped.textureWidth;
		int textureHeight = prepped.textureHeight;

		textureIdMap[textureIndex] = i;
		node->textureIndex = i;
		// node->setTexture(glm::ivec2(textureWidth, textureHeight), textureData.data());
		// make a fake proxy texture without actual OpenGL Buffer behind it
		node->texture = make_shared<GLTexture>();
		node->texture->width = textureWidth;
		node->texture->height = textureHeight;

		for(int i = 0; i < prepped.indexerLevels.size(); i++){
			editor->textureIndexer.push_back(prepped.indexerLevels[i]);
		}

		container->children.push_back(node);
	}
	scene.world->children.push_back(container);

	printElapsedTime("Time until all sent to gpu", t_start);

	return container;
}

void initScene() {
	SplatEditor* editor = SplatEditor::instance;
	// Scene& scene = editor->scene;

	// position: 124.54672426747658, -42.72048538939598, -12.2730454323992 
	Runtime::controls->yaw    = -5.179;
	Runtime::controls->pitch  = 0.108;
	Runtime::controls->radius = 142.656;
	Runtime::controls->target = { -2.859, 21.085, -5.387, };

	// TEST SCENE: CUBE
	auto createCube = [&](){ 

		// Create a cube and <n> duplicates of a texture to test whether many textures impact rendering performance.
		// Enable rendering of duplicates in mesh.fs code.

		std::string file = "./resources/meshes/Cube_50.glb";
		auto node = loadModel(file, 50);

		// position: -3.1311160713047617, -2.4088701201382867, 1.1629675067265095 
		Runtime::controls->yaw    = -13.436;
		Runtime::controls->pitch  = -0.490;
		Runtime::controls->radius = 4.195;
		Runtime::controls->target = { 0.074, -0.021, -0.111, };

		// CREATE TEXTURES
		string texturePath = "D:/dev/workspaces/jpeg/resources/textures/brick_wall_006_diff_4k_q60.jpg";

		shared_ptr<Buffer> data = readBinaryFile(texturePath);
		vector<uint8_t> datavu8 = vector<uint8_t>(data->data_u8, data->data_u8 + data->size);
		
		int numDuplicates = 10;
		vector<JPEGIndexer> jpegs(8 * numDuplicates);
		for(int texIdx = 0; texIdx < numDuplicates; texIdx++){
			
			// MIP MAPS FOR JPEG
			// jpegs.push_back(JPEGIndexer(datavu8));
			jpegs[8 * texIdx] = JPEGIndexer(datavu8);
			int width = jpegs[8 * texIdx].width;
			int height = jpegs[8 * texIdx].height;

			for (int i = 1; i < 8; i++) {
				vector<uint8_t>test_jpeg;

				if (width / pow(2, i) < 4 || height / pow(2, i) < 4) {
					resize_jpeg_buffer_turbo(datavu8, 4, 4, test_jpeg);
				} else {
					resize_jpeg_buffer_turbo(datavu8, width / pow(2,i), height / pow(2,i), test_jpeg);
				}

				JPEGIndexer indexer = JPEGIndexer(test_jpeg);
				indexer.mipMapLevel = i;
				// jpegs.push_back(indexer);
				jpegs[8 * texIdx + i] = indexer;
			}

		}

		for(int i = 0; i < jpegs.size(); i++){
			editor->textureIndexer.push_back(jpegs[i]);
		}
	};

	// TEST SCENE: SPONZA
	auto loadSponza = [](){ 
		std::string file = "./resources/meshes/Sponza_70.glb";
		auto node = loadModel(file, 70);

		// Sponza does not seem to be in meters?
		// Scale it down a bit, and add an offset so that it is somewhat nicely aligned for VR
		float s = 0.01f;
		vec3 offset = vec3{0.0f, 0.0f, -1.4f};
		mat4 scale = glm::scale(vec3{s, s, s});
		mat4 flip = mat4(
			1.000,  0.000, 0.000, 0.000,
			0.000,  0.000, 1.000, 0.000,
			0.000, -1.000, 0.000, 0.000,
			0.000,  0.000, 0.000, 1.000);
		mat4 translate = glm::translate(offset);
		node->transform = translate * flip * scale;

		Runtime::controls->yaw    = -4.706;
		Runtime::controls->pitch  = -0.010;
		Runtime::controls->radius = 655.503 * s;
		Runtime::controls->target = (vec3{ -50.706, 34.916, 186.831} * s) + offset;
	};

	// TEST SCENE: GRAFFITI
	auto loadGraffiti = [](){
		std::string file = "./resources/meshes/donaukanal_urania_1M_jpeg80.glb";
		auto node = loadModel(file, 80);

		float s = 0.1f;
		vec3 offset = vec3{-2.0f, -6.0f, -1.5f};
		mat4 scale = glm::scale(vec3{s, s, s});
		mat4 translate = glm::translate(offset);
		node->transform = translate  * scale;

		// position: 111.18623005016221, 173.445770160369, 20.61328306568038 
		Runtime::controls->yaw    = -4.373;
		Runtime::controls->pitch  = -0.028;
		Runtime::controls->radius = 189.876 * s;
		Runtime::controls->target = (vec3{ -67.862, 110.268, 22.351} * s) + offset;
	};

	// createCube();
	// loadSponza();
	loadGraffiti();
}


int main(){

	initCuda();
	GLRenderer::init();
	SplatEditor::setup();
	initScene();

	GLRenderer::loop(
		[&]() {SplatEditor::instance->update();},
		[&]() {SplatEditor::instance->render();},
		[&]() {SplatEditor::instance->postFrame();}
	);

	return 0;
}
