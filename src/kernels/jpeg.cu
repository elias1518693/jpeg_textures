#define CUB_DISABLE_BF16_SUPPORT

// === required by GLM ===
#define GLM_FORCE_CUDA
#define CUDA_VERSION 12000
namespace std {
	using size_t = ::size_t;
};
// =======================

#include <curand_kernel.h>
#include <cooperative_groups.h>

#include "./libs/glm/glm/glm.hpp"
#include "./libs/glm/glm/gtc/matrix_transform.hpp"
#include "./libs/glm/glm/gtc/matrix_access.hpp"
#include "./libs/glm/glm/gtx/transform.hpp"
#include "./libs/glm/glm/gtc/quaternion.hpp"

#include "./utils.cuh"
#include "./BitReaderGPU.cuh"
#include "./HostDeviceInterface.h"
#include "./HashMap.cuh"
#include "./dct.cuh"

using glm::ivec2;
using glm::i8vec4;

constexpr uint32_t UV_BITS = 12;
constexpr float UV_FACTOR = 1 << UV_BITS;

struct Decoded {
	vec2 uv;
	int id;
};

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
	int width;
	int height;
	uint8_t* data;
	HuffmanTable* huffmanTables;
	QuantizationTable* quanttables;
	uint32_t* mcuPositions;
};

#define RGBA(r, g, b) ((uint32_t(255) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b))

// retrieve the 4 texels around the given uv coordinate
void getTexels(
	uint32_t texID,
	vec2 uv, 
	TextureData* tex, 
	uint32_t* decoded,
	HashMap& decodedMcuMap,
	vec4* t00,
	vec4* t01,
	vec4* t10,
	vec4* t11
){
	float ftx = fmodf(uv.x, 1.0f) * float(tex->width);
	float fty = fmodf(uv.y, 1.0f) * float(tex->height);

	float ftlx = fmodf(ftx, 16.0f);
	float ftly = fmodf(fty, 16.0f);

	*t00 = {0.0f, 0.0f, 0.0f, 255.0f};
	*t01 = {0.0f, 0.0f, 0.0f, 255.0f};
	*t10 = {0.0f, 0.0f, 0.0f, 255.0f};
	*t11 = {0.0f, 0.0f, 0.0f, 0.0f};

	auto toVec4 = [](uint32_t color){
		return vec4{
			(color >>  0) & 0xff,
			(color >>  8) & 0xff,
			(color >> 16) & 0xff,
			(color >> 24) & 0xff,
		}; 
	};

	// EVALUATION: Clamp to a single MCU to check impact of accessing single vs. multiple MCUs
	// ftlx = clamp(ftlx, 0.6f, 15.4f);
	// ftly = clamp(ftly, 0.6f, 15.4f);
	// ftx = floor(ftx / 16.0f) * 16.0f + ftlx;
	// fty = floor(fty / 16.0f) * 16.0f + ftly;

	if(ftlx > 0.5f && ftlx < 15.5f && ftly > 0.5f && ftly < 15.5f){
		// Easy and fast case: All texels in same MCU

		int tx = ftx - 0.5f;
		int ty = fty - 0.5f;

		int mcu = tx / 16 + ty / 16 * tex->width / 16;
		uint32_t key = ((mcu & 0xffff) << 16) | (texID & 0xffff);

		uint32_t value;
		if(decodedMcuMap.get(key, &value)){
			uint32_t decodedMcuIndex = value & 0x00ffffff;
			
			tx %= 16;
			ty %= 16;
			int offset_00 = (tx + 0) % 8 + ((tx + 0) / 8) * 64 + ((ty + 0) % 8) * 8 + ((ty + 0) / 8) * 128;
			int offset_01 = (tx + 0) % 8 + ((tx + 0) / 8) * 64 + ((ty + 1) % 8) * 8 + ((ty + 1) / 8) * 128;
			int offset_10 = (tx + 1) % 8 + ((tx + 1) / 8) * 64 + ((ty + 0) % 8) * 8 + ((ty + 0) / 8) * 128;
			int offset_11 = (tx + 1) % 8 + ((tx + 1) / 8) * 64 + ((ty + 1) % 8) * 8 + ((ty + 1) / 8) * 128;

			*t00 = toVec4(decoded[decodedMcuIndex * 256 + offset_00]);
			*t01 = toVec4(decoded[decodedMcuIndex * 256 + offset_01]);
			*t10 = toVec4(decoded[decodedMcuIndex * 256 + offset_10]);
			*t11 = toVec4(decoded[decodedMcuIndex * 256 + offset_11]);

			// { // EVALUATION: Only for vr-caching figure. Set in comments when not in use. 
			// 	uint32_t flag = (value & 0xff000000) >> 24;
			// 	if(flag == 0xff){
			// 		*t00 = *t00 * 0.5f + 0.5f * vec4{255.0f, 0.0f, 0.0f, 255.0f};
			// 		*t01 = *t01 * 0.5f + 0.5f * vec4{255.0f, 0.0f, 0.0f, 255.0f};
			// 		*t10 = *t10 * 0.5f + 0.5f * vec4{255.0f, 0.0f, 0.0f, 255.0f};
			// 		*t11 = *t11 * 0.5f + 0.5f * vec4{255.0f, 0.0f, 0.0f, 255.0f};
			// 	}else{
			// 		*t00 = *t00 * 0.5f + 0.5f * vec4{0.0f, 255.0f, 0.0f, 255.0f};
			// 		*t01 = *t01 * 0.5f + 0.5f * vec4{0.0f, 255.0f, 0.0f, 255.0f};
			// 		*t10 = *t10 * 0.5f + 0.5f * vec4{0.0f, 255.0f, 0.0f, 255.0f};
			// 		*t11 = *t11 * 0.5f + 0.5f * vec4{0.0f, 255.0f, 0.0f, 255.0f};
			// 	}
			// }
		}
	}else{
		
		// Trickier case: texels reside in adjacent MCUs, which may or may not be available.
		uint32_t v_00, v_01, v_10, v_11 = 0;

		auto texelCoordToKey = [&](int tx, int ty){
			uint32_t mcu = tx / 16 + ty / 16 * tex->width / 16;
			uint32_t key = ((mcu & 0xffff) << 16) | (texID & 0xffff);
			return key;
		};

		bool v00Exists = decodedMcuMap.get(texelCoordToKey(ftx - 0.5f, fty - 0.5f), &v_00);
		bool v01Exists = decodedMcuMap.get(texelCoordToKey(ftx - 0.5f, fty + 0.5f), &v_01);
		bool v10Exists = decodedMcuMap.get(texelCoordToKey(ftx + 0.5f, fty - 0.5f), &v_10);
		bool v11Exists = decodedMcuMap.get(texelCoordToKey(ftx + 0.5f, fty + 0.5f), &v_11);

		auto toTexel = [&](uint32_t value, int tx, int ty){
			uint32_t decodedMcuIndex = value & 0x00ffffff;

			tx %= 16;
			ty %= 16;
			int offset = tx % 8 + (tx / 8) * 64 + (ty% 8) * 8 + (ty / 8) * 128;

			vec4 color = toVec4(decoded[decodedMcuIndex * 256 + offset]);

			// { // EVALUATION: Only for vr-caching figure. Set in comments when not in use. 
			// 	uint32_t flag = (value & 0xff000000) >> 24;
			// 	if(flag == 0xff){
			// 		return 0.5f * color + 0.5f * vec4{255.0f, 0.0f, 0.0f, 255.0f};
			// 	}else{
			// 		return 0.5f * color + 0.5f * vec4{0.0f, 255.0f, 0.0f, 255.0f};
			// 	}
			// }

			return color;
		};

		// If a texel's MCU is not decoded, clamp to one of the decoded MCUs
		
		// texel 00
		if(v00Exists)        *t00 = toTexel(v_00, ftx - 0.5f, fty - 0.5f);
		else if(v10Exists)   *t00 = toTexel(v_10, ftx + 0.5f, fty - 0.5f);
		else if(v01Exists)   *t00 = toTexel(v_01, ftx - 0.5f, fty + 0.5f);
		else if(v11Exists)   *t00 = toTexel(v_11, ftx + 0.5f, fty + 0.5f);
		
		// texel 01
		if(v01Exists)        *t01 = toTexel(v_01, ftx - 0.5f, fty + 0.5f);
		else if(v00Exists)   *t01 = toTexel(v_00, ftx - 0.5f, fty - 0.5f);
		else if(v10Exists)   *t01 = toTexel(v_10, ftx + 0.5f, fty - 0.5f);
		else if(v11Exists)   *t01 = toTexel(v_11, ftx + 0.5f, fty + 0.5f);

		// texel 10
		if(v10Exists)        *t10 = toTexel(v_10, ftx + 0.5f, fty - 0.5f);
		else if(v00Exists)   *t10 = toTexel(v_00, ftx - 0.5f, fty - 0.5f);
		else if(v01Exists)   *t10 = toTexel(v_01, ftx - 0.5f, fty + 0.5f);
		else if(v11Exists)   *t10 = toTexel(v_11, ftx + 0.5f, fty + 0.5f);

		// texel 11
		if(v11Exists)        *t11 = toTexel(v_11, ftx + 0.5f, fty + 0.5f);
		else if(v00Exists)   *t11 = toTexel(v_00, ftx - 0.5f, fty - 0.5f);
		else if(v01Exists)   *t11 = toTexel(v_01, ftx - 0.5f, fty + 0.5f);
		else if(v10Exists)   *t11 = toTexel(v_10, ftx + 0.5f, fty - 0.5f);
	}
}

uint32_t sampleColor_nearest(
	uint32_t texID,
	vec2 uv, 
	TextureData* texturesData, 
	uint32_t* decoded,
	HashMap& decodedMcuMap
){

	uint32_t color = 0;
	auto tex = &texturesData[texID];
	int tx = (int(uv.x * tex->width) % tex->width);
	int ty = (int(uv.y * tex->height) % tex->height);
	int mcu = tx / 16 + ty / 16 * tex->width / 16;
	uint32_t key = ((mcu & 0xffff) << 16) | (texID & 0xffff);

	uint32_t value;
	if(decodedMcuMap.get(key, &value)){
		uint32_t decodedMcuIndex = value & 0x00ffffff;
		bool isNewlyDecoded = (value >> 31) != 0;

		int tx = (int(uv.x * tex->width) % tex->width);
		int ty = (int(uv.y * tex->height) % tex->height);

		tx %= 16;
		ty %= 16;
		int offset = tx % 8 + (tx / 8) * 64 + (ty % 8) * 8 + (ty / 8) * 128;
		color = decoded[decodedMcuIndex * 256 + offset];

	}else{
		color = 0x00000000;
	}

	return color;
}

uint32_t sampleColor_linear(
	uint32_t texID,
	vec2 uv, 
	TextureData* texturesData, 
	uint32_t* decoded,
	HashMap& decodedMcuMap
){

	uint32_t color = 0xff000000;
	uint8_t* rgba = (uint8_t*)&color;
	auto tex = &texturesData[texID];

	// Apply 0.5f offset so that interpolated colors align with nearest-neighbor colors, i.e., the color sample is at the center.
	float ftx = fmodf(uv.x - 0.5f / float(tex->width), 1.0f) * float(tex->width);
	float fty = fmodf(uv.y - 0.5f / float(tex->height), 1.0f) * float(tex->height);

	vec4 t00, t01, t10, t11;
	getTexels(texID, uv, tex, decoded, decodedMcuMap, &t00, &t01, &t10, &t11);

	float wx = fmodf(ftx, 1.0f);
	float wy = fmodf(fty, 1.0f);

	vec4 interpolated = 
		(1.0f - wx) * (1.0f - wy) * t00 + 
		wx * (1.0f - wy) * t10 + 
		(1.0f - wx) * wy * t01 + 
		wx * wy * t11;

	rgba[0] = interpolated.r;
	rgba[1] = interpolated.g;
	rgba[2] = interpolated.b;
	rgba[3] = 255;

	return color;
}

void idct8x8_optimized(float* block, int thread) {
	auto cuda_block = cg::this_thread_block();

	cuda_block.sync();

	// Perform IDCT on rows
	CUDAsubroutineInplaceIDCTvector(&block[thread * 8], 1);

	cuda_block.sync();

	// Perform IDCT on columns
	CUDAsubroutineInplaceIDCTvector(&block[thread], 8);
}

int decodeHuffman(BitReaderGPU& bit_reader, const HuffmanTable& huffman_table) {
	int code = 0;
	int offset = 0;
	for (int bit_length = 1; bit_length <= 16; bit_length++) {
		code = (code << 1) | bit_reader.read_bit();
		int code_count = huffman_table.num_codes_per_bit_length[bit_length - 1];
#pragma unroll
		for (int j = 0; j < code_count; j++) {
			if (huffman_table.huffman_keys[offset + j] == code) {
				return huffman_table.huffman_values[offset + j];
			}
		}
		offset += huffman_table.num_codes_per_bit_length[bit_length - 1];
	}
	return -1;
}

int decodeHuffman_warpwide(BitReaderGPU& bit_reader, const HuffmanTable& huffman_table) {

	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();
	auto warp = cg::tiled_partition<32>(block);
	auto index = grid.thread_rank();

	uint32_t code_peek = bit_reader.peek16Bit2();

	// decode up to 128 huffman codes. (4 iterations, 32 codes per iteration)
	// changed to 192 (6 iterations, 32 codes)
	#pragma unroll
	for(int i = 0; i < 6; i++){
		int codeIndex = i * 32 + warp.thread_rank();
		uint32_t bit_length = huffman_table.codelengths[codeIndex];
		uint32_t code = code_peek >> (16 - bit_length);
		bool isValidCode = huffman_table.huffman_keys[codeIndex] == code;

		uint32_t mask = warp.ballot(isValidCode);

		if(mask > 0){
			int winningLane = __ffs(mask) - 1;

			// the winning lane broadcasts the huffman value and the bit length to all other threads
			bit_length = warp.shfl(bit_length, winningLane);
			uint32_t huffmanValue = warp.shfl(huffman_table.huffman_values[codeIndex], winningLane);

			bit_reader.advance(bit_length);

			return huffmanValue;
		}
	}

	return -1;
}

int DecodeNumber(int code, int bits) {
	int l = 1 << (code - 1);
	if (bits >= l) {
		return bits;
	}
	else {
		return bits - (2 * l - 1);
	}
}

void decodeCoefficients(BitReaderGPU& bit_reader, HuffmanTable& huffman_table, float* coefficients, int previous_dc) {
	coefficients[0] = previous_dc;
	int i = 1;
#pragma unroll
	while (i < 64) {
		int ac_code = decodeHuffman(bit_reader, huffman_table);

		if (ac_code == 0) {
			break;
		}
		int size = ac_code;
		if (ac_code > 15) {
			int run_length = (ac_code >> 4) & 0xF;
			size = ac_code & 0xF;
			i += run_length;
		}
		if (i >= 64) break;
		int ac_value = bit_reader.read_bits(size);
		ac_value = DecodeNumber(size, ac_value);
		coefficients[i++] = ac_value;
	}
}

void decodeCoefficients_warpwide(
	BitReaderGPU& bit_reader,
	const HuffmanTable& huffman_table,
	float* sh_coefficients
) {
	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();
	auto warp = cg::tiled_partition<32>(block);
	int i = 1;
#pragma unroll
	while (i < 64) {
		int ac_code = decodeHuffman_warpwide(bit_reader, huffman_table);

		if (ac_code == 0) {
			break;
		}
		int size = ac_code;
		if (ac_code > 15) {
			int run_length = (ac_code >> 4) & 0xF;
			size = ac_code & 0xF;
			i += run_length;
		}

		if (i >= 64) break;

		int ac_value = bit_reader.read_bits(size);
		ac_value = DecodeNumber(size, ac_value);

		sh_coefficients[i++] = ac_value;

		// warp.sync();
	}
}


__constant__ int dezigzag_order[64] = {
	0, 1, 8, 16, 9, 2, 3, 10,
 17, 24, 32, 25, 18, 11, 4, 5,
 12, 19, 26, 33, 40, 48, 41, 34,
 27, 20, 13, 6, 7, 14, 21, 28,
 35, 42, 49, 56, 57, 50, 43, 36,
 29, 22, 15, 23, 30, 37, 44, 51,
 58, 59, 52, 45, 38, 31, 39, 46,
 53, 60, 61, 54, 47, 55, 62, 63
};

// fetch bit-offset to the mcu from the indexing table
int calculate_datastart(int mcu, const uint32_t* mcu_index) {
	int packed_index = (mcu / 9) * 5;
	int offset_within_packed = mcu % 9;
	int absolute_offset = mcu_index[packed_index];
	if (offset_within_packed == 0) {
		return absolute_offset;
	}

	int rel_index = offset_within_packed - 1; 
	int word = mcu_index[packed_index + 1 + (rel_index / 2)];
	int shift = (rel_index % 2 == 0) ? 16 : 0;
	int relative_offset = (word >> shift) & 0xFFFF;
	return absolute_offset + relative_offset;
}

uint16_t get12bit(const uint8_t* buf, int idx) {
	int group = idx / 2;
	int byte_idx = group * 3;

	if ((idx % 2) == 0) {
		return (buf[byte_idx]) | ((buf[byte_idx + 1] & 0x0F) << 8);
	}
	else {
		return ((buf[byte_idx + 1] >> 4) & 0x0F) | (buf[byte_idx + 2] << 4);
	}
}

uint32_t uvToMCUIndex(int width, int height, float u, float v) {
	int tx = (int(u * width) % width);
	int ty = (int(v * height) % height);
	return tx / 16 + ty / 16 * width / 16;
}

extern "C" __global__
void kernel_mark(
	uint32_t width, 
	uint32_t height,
	cudaSurfaceObject_t gl_uvs,
	cudaSurfaceObject_t gl_miplevel,
	uint32_t* toDecode,
	uint32_t* toDecodeCounter,
	TextureData* texturesData,
	int num_textures,
	HashMap decodedMcuMap
) {
	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();

	// int x = grid.block_index().x * 8 + block.thread_index().x;
	// int y = grid.block_index().y * 8 + block.thread_index().y;

	int pixelID = grid.thread_rank();
	int x = pixelID % width;
	int y = pixelID / width;

	if(x >= width) return;
	if(y >= height) return;


	uint32_t pixelVal;
	surf2Dread(&pixelVal, gl_uvs, x * 4, y);
	uint32_t uvX = (pixelVal >>  0) & 0xffff;
	uint32_t uvY = (pixelVal >> 16) & 0xffff;
	vec2 uv = {float(uvX) / 65536.0f, float(uvY) / 65536.0f};

	uint32_t pixelMiplevel;
	surf2Dread(&pixelMiplevel, gl_miplevel, x * 4, y);
	uint32_t mipLevel = min(float((pixelMiplevel >> 0) & 0xffff) / 256.0f, 7.0);
	uint32_t texID = (pixelMiplevel >> 16) & 0xffff;
	uint32_t texID_mipmap = texID * 8 + mipLevel;

	if (texID >= num_textures) return;
	
	// key: mcu's are identified by their index, texture-id, and mip map level. 
	uint32_t mcu = uvToMCUIndex(texturesData[texID_mipmap].width, texturesData[texID_mipmap].height, uv.x, uv.y);
	uint32_t key = ((mcu & 0xffff) << 16) | (texID_mipmap & 0xffff);

	// to avoid contention, make sure that for any MCU, only one thread per warp continues.
	{ 
		// mask of warp threads with same key
		auto block = cg::this_thread_block();
		auto warp = cg::tiled_partition<32>(block);
		uint32_t mask = warp.match_any(key);

		// find the lowest lane between threads with the same key
		int winningLane = __ffs(mask) - 1;

		// return early because another thread handles this MCU
		if(warp.thread_rank() != winningLane) return;
	}

	// Reserve a spot in the hash map. The value, the TB-Slot, will be acquired and set by the decode kernel
	bool alreadyExists = false;
	int location = 0;
	decodedMcuMap.set(key, 0, &location, &alreadyExists);

	if(!alreadyExists){
		// Add MCU to decoder queue
		uint32_t decodeIndex = atomicAdd(toDecodeCounter, 1);
		toDecode[decodeIndex] = key;
	}else{
		// MCU is in cache - flag as visible
		atomicOr(&decodedMcuMap.entries[location], 0x00000000'ff000000);
	}

}

extern "C" __global__
void kernel_decode_420(
	uint32_t* toDecode,
	uint32_t* decoded,
	TextureData* texturesData,
	HashMap decodedMcuMap,
	uint32_t* TBSlots,
	uint32_t firstAvailableTBSlotsIndex
) {
	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();
	auto warp = cg::tiled_partition<32>(block);
	int thread = block.thread_rank();

	uint32_t textureInfo = toDecode[grid.block_rank()];
	uint32_t texture_id = ((textureInfo >> 0) & 0xffff);
	uint32_t mcu = (textureInfo >> 16) & 0xffff;

	block.sync();

	__shared__ float sh_coefficients[384];
	__shared__ float sh_dezigzagged[510];
	__shared__ uint8_t sh_data[510];

	const TextureData& textureData = texturesData[texture_id];

	int datastart = calculate_datastart(mcu, textureData.mcuPositions);
	int datastartbit = datastart % 8;

	// Load compressed data into shared memory
	for (int i = 0; i < 6; i++) {
		sh_data[block.thread_rank() + 64 * i] = textureData.data[datastart / 8 + block.thread_rank() + 64 * i];
		sh_coefficients[block.thread_rank() + 64 * i] = 0;
	}

	block.sync();

	BitReaderGPU bit_reader(&sh_data[0], datastartbit);

	// Decode DC's with first warp
	if (warp.meta_group_rank() == 0)
	{
		int previousDC = (bit_reader.read_bits(12) & 0x0fff) - 2048;
		sh_coefficients[0] = previousDC;
		for (int i = 1; i < 4; i++) {

			int huff_index = (i <= 3) ? 0 : (i - 3);
			const HuffmanTable& huffmanTable = textureData.huffmanTables[huff_index];
			int dc_value = decodeHuffman_warpwide(bit_reader, huffmanTable);
			if (dc_value > 0) {
				int dc_difference = bit_reader.read_bits(dc_value);
				dc_value = DecodeNumber(dc_value, dc_difference);
			}

			sh_coefficients[i * 64] = dc_value + previousDC;
			previousDC = dc_value + previousDC;
		}
		sh_coefficients[64 * 4] = (bit_reader.read_bits(12) & 0x0fff) - 2048;
		sh_coefficients[64 * 5] = (bit_reader.read_bits(12) & 0x0fff) - 2048;
	}

	block.sync();

	// Decode AC's with first warp
	if (warp.meta_group_rank() == 0)
	{
		for (int i = 0; i < 6; i++) {
			int huff_index = (i <= 3) ? 0 : (i - 3);

			const HuffmanTable& huffmanTable = textureData.huffmanTables[3 + huff_index];
			int previousDC = 0;

			decodeCoefficients_warpwide(bit_reader, huffmanTable, &sh_coefficients[i * 64]);
		}
	}

	block.sync();

	QuantizationTable* quanttable1 = &textureData.quanttables[0];
	QuantizationTable* quanttable2 = &textureData.quanttables[1];
	for (int i = 0; i < 4; i++)
		sh_dezigzagged[dezigzag_order[thread] + i * 64] = sh_coefficients[threadIdx.x + 64 * i] * quanttable1->values[thread];
	for (int i = 0; i < 2; i++)
		sh_dezigzagged[dezigzag_order[thread] + i * 64 + 256] = sh_coefficients[threadIdx.x + 256 + 64 * i] * quanttable2->values[thread];

	block.sync();

	idct8x8_optimized(&sh_dezigzagged[(thread / 8) * 64], thread % 8);

	block.sync();

	// Acquire a texture block cache slot
	__shared__ int sh_tbslot;
	if(block.thread_rank() == 0){
		uint32_t slotIndex = firstAvailableTBSlotsIndex + grid.block_rank();
		uint32_t tbslot = TBSlots[slotIndex];
		uint32_t visFlag = 0b0000'0001; // mark as visible & newly cached. 
		uint32_t value = (visFlag << 24) | tbslot;
		uint64_t entry = (uint64_t(textureInfo) << 32) | uint64_t(value);
		
		bool alreadyExists = false;
		int location = 0;
		decodedMcuMap.set(textureInfo, 0, &location, &alreadyExists);
		atomicExch(&decodedMcuMap.entries[location], entry);

		sh_tbslot = tbslot;
	}

	block.sync();

	// Write decoded texels to texture block cache
	for (int i = 0; i < 4; i++) {
		uint8_t* rgba = (uint8_t*)&decoded[sh_tbslot * 256 + threadIdx.x + 64 * i];
		float y = sh_dezigzagged[threadIdx.x + 64 * i] + 128.0f;

		int chroma_x = (thread % 8) / 2;
		int chroma_y = (thread / 8) / 2;
		int chroma_index = chroma_y * 8 + chroma_x + i / 2 * 4 * 8 + i % 2 * 4;

		float cb = sh_dezigzagged[chroma_index + 64 * 4];
		float cr = sh_dezigzagged[chroma_index + 64 * 5];

		rgba[0] = clamp(y + 1.402f * cr, 0.0f, 255.0f);
		rgba[1] = clamp(y - 0.344136f * cb - 0.714136f * cr, 0.0f, 255.0f);
		rgba[2] = clamp(y + 1.772f * cb, 0.0f, 255.0f);
		rgba[3] = 255;
	}
}


// This kernel is used to indirectly launch kernel_decode_420 from the GPU, 
// so that we don't have to memcpy <toDecodeCounter> to host before launching it. 
extern "C" __global__
void kernel_launch_decode(
	uint32_t* toDecodeCounter,
	uint32_t* TBSlots,
	uint32_t* TBSlotsCounter,
	uint32_t* toDecode,
	uint32_t* decoded,
	TextureData* texturesData,
	HashMap decodedMcuMap
) {
	auto grid = cg::this_grid();

	if(grid.thread_rank() == 0){

		int numBlocks = *toDecodeCounter;
		uint32_t firstAvailableTBSlotsIndex = *TBSlotsCounter;

		kernel_decode_420<<<numBlocks, 64>>>(
			toDecode,
			decoded,
			texturesData,
			decodedMcuMap,
			TBSlots,
			*TBSlotsCounter
		);

		*TBSlotsCounter = (*toDecodeCounter) + (*TBSlotsCounter);
	}
}

extern "C" __global__
void kernel_resolve(
	CommonLaunchArgs args,
	uint32_t viewIndex,
	uint32_t width, 
	uint32_t height,
	cudaSurfaceObject_t gl_desktop,
	// cudaSurfaceObject_t gl_desktop_depth,
	cudaSurfaceObject_t gl_uvs,
	cudaSurfaceObject_t gl_miplevel,
	bool showUVs,
	uint32_t* toDecode,
	uint32_t* decoded,
	TextureData* texturesData,
	int num_textures,
	HashMap decodedMcuMap
) {

	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();

	int x = grid.block_index().x * 8 + block.thread_index().x;
	int y = grid.block_index().y * 8 + block.thread_index().y;

	if(x >= width) return;
	if(y >= height) return;

	uint32_t pixelUVs;
	surf2Dread(&pixelUVs, gl_uvs, x * 4, y);

	uint32_t uvX = (pixelUVs >>  0) & 0xffff;
	uint32_t uvY = (pixelUVs >> 16) & 0xffff;
	vec2 uv = {float(uvX) / 65536.0f, float(uvY) / 65536.0f};

	uint32_t pixelMiplevel;
	surf2Dread(&pixelMiplevel, gl_miplevel, x * 4, y);
	uint32_t mipLevel = min(float((pixelMiplevel >> 0) & 0xffff) / 256.0f, 7.0);
	uint32_t texID = (pixelMiplevel >> 16) & 0xffff;
	uint32_t texID_mipmap = texID * 8 + mipLevel;
	if (pixelUVs == 0) return;

	uint32_t color = 0;
	uint8_t* rgba = (uint8_t*)&color;

	if(args.uniforms.enableLinearInterpolation){
		color = sampleColor_linear(texID_mipmap, uv, texturesData, decoded, decodedMcuMap);
	}else{
		color = sampleColor_nearest(texID_mipmap, uv, texturesData, decoded, decodedMcuMap);
	}

	// if(viewIndex > 0)
	if(args.uniforms.showCaching){
		auto tex = &texturesData[texID_mipmap];
		int tx = (int(uv.x * tex->width) % tex->width);
		int ty = (int(uv.y * tex->height) % tex->height);
		int mcu = tx / 16 + ty / 16 * tex->width / 16;
		uint32_t key = ((mcu & 0xffff) << 16) | (texID_mipmap & 0xffff);
		uint32_t value;
		if(decodedMcuMap.get(key, &value)){

			uint32_t decodedMcuIndex = value & 0x00ffffff;
			bool isNewlyDecoded = (value >> 24) == 0b00000001;

			if(isNewlyDecoded){
				// color = 0xff0000ff;
				rgba[0] = float(rgba[0]) * 0.5f + 125.0f;
				rgba[1] = float(rgba[1]) * 0.5f + 0.0f;
				rgba[2] = float(rgba[2]) * 0.5f + 0.0f;
			}else{
				// color = 0xff00ff00;
				rgba[0] = float(rgba[0]) * 0.5f + 0.0f;
				rgba[1] = float(rgba[1]) * 0.5f + 125.0f;
				rgba[2] = float(rgba[2]) * 0.5f + 0.0f;
			}
		}
	}else if(args.uniforms.showUVs){
		rgba[0] = 256.0f * uv.x;
		rgba[1] = 256.0f * uv.y;
		rgba[2] = 0;
	}else if(args.uniforms.showMCUs){
		auto tex = &texturesData[texID_mipmap];
		int tx = (int(uv.x * tex->width) % tex->width);
		int ty = (int(uv.y * tex->height) % tex->height);
		int mcu = tx / 16 + ty / 16 * tex->width / 16;
		color = mcu * 123456;

		rgba[0] = (texID * 1234) % 256;

		color = mipLevel * 12345;
		color = mcu * texID * mipLevel * 12345;


		int level = float(mipLevel) * 14.0f / 7.0f;
		if(level == 10) color = 0xff42019e;
		if(level ==  9) color = 0xff4f3ed5;
		if(level ==  8) color = 0xff436df4;
		if(level ==  7) color = 0xff61aefd;
		if(level ==  6) color = 0xff8be0fe;
		if(level ==  5) color = 0xffbfffff;
		if(level ==  4) color = 0xff98f5e6;
		if(level ==  3) color = 0xffa4ddab;
		if(level ==  2) color = 0xffa5c266;
		if(level ==  1) color = 0xffbd8832;
		if(level ==  0) color = 0xffa24f5e;

		float wMcu = 0.0;
		if((tx / 16 + ty / 16) % 2 == 0){
			wMcu = 1.0f;
		}else{
			wMcu = 0.8f;
		}

		uint32_t c2 = texID * 12345678901898;
		uint8_t* rgbac2 = (uint8_t*)&c2;


		rgba[0] = clamp((0.5f * float(rgba[0]) + 0.5f * float(rgbac2[0])) * wMcu, 0.0f, 255.0f);
		rgba[1] = clamp((0.5f * float(rgba[1]) + 0.5f * float(rgbac2[1])) * wMcu, 0.0f, 255.0f);
		rgba[2] = clamp((0.5f * float(rgba[2]) + 0.5f * float(rgbac2[2])) * wMcu, 0.0f, 255.0f);
	}else if(args.uniforms.showTexID){
		color = texID * 123456;
	}else if(args.uniforms.showMipLevel){
		int level = float(mipLevel) * 14.0f / 7.0f;
		if(level == 10) color = 0xff42019e;
		if(level ==  9) color = 0xff4f3ed5;
		if(level ==  8) color = 0xff436df4;
		if(level ==  7) color = 0xff61aefd;
		if(level ==  6) color = 0xff8be0fe;
		if(level ==  5) color = 0xffbfffff;
		if(level ==  4) color = 0xff98f5e6;
		if(level ==  3) color = 0xffa4ddab;
		if(level ==  2) color = 0xffa5c266;
		if(level ==  1) color = 0xffbd8832;
		if(level ==  0) color = 0xffa24f5e;
	}

	// color = texID * 12345;
	color = color | 0xff000000;

	// x = clamp(x, 0, width - 1);
	// y = clamp(y, 0, height - 1);

	// if(x == 0 && y == 0) printf("width: %d, height: %d \n", width, height);
	// color = 0xff0000ff;

	surf2Dwrite(color, gl_desktop, x * 4, y);
}


extern "C" __global__
void kernel_init_availableMcuSlots(
	uint32_t* TBSlots,
	uint32_t* TBSlotsCounter,
	uint32_t numDecodedMcuCapacity
) {

	auto grid = cg::this_grid();

	if(grid.thread_rank() >= numDecodedMcuCapacity) return;

	TBSlots[grid.thread_rank()] = grid.thread_rank();
}

extern "C" __global__
void kernel_update_cache(
	HashMap decodedMcuMap_source,
	HashMap decodedMcuMap_target,
	uint32_t* TBSlots,
	uint32_t* TBSlotsCounter,
	bool freezeCache
) {

	auto grid = cg::this_grid();

	if(grid.thread_rank() >= decodedMcuMap_source.capacity) return;

	uint64_t entry = decodedMcuMap_source.entries[grid.thread_rank()];
	uint32_t key = entry >> 32;
	uint32_t value = entry & 0xffffffff;
	uint32_t visFlag  = (value >> 24) & 0xff;
	uint32_t slot = (value >>  0)  & 0xffffff;

	bool isMcuVisible = visFlag != 0;
	bool isNewlyDecoded = (visFlag == 0b00000001);

	// Note: If cache is frozen: Only remove newly decoded entries 
	// but preserve previously cached entries, including those that are currently invisible. 

	if(entry == HashMap::EMPTYENTRY) return;

	// Put slot to decoded texture block back in pool of slots
	auto remove = [&](){
		uint32_t old = atomicSub(TBSlotsCounter, 1);
		int slotIndex = int(old) - 1;

		TBSlots[slotIndex] = slot;
	};

	// Replicate entry in new hash map, which will be used in the next frame. 
	auto preserve = [&](){
		int location;
		bool alreadyExists;
		uint32_t newVal = (0x00 << 24) | slot;
		decodedMcuMap_target.set(key, newVal, &location, &alreadyExists);
	};

	if(freezeCache){
		if(isNewlyDecoded){
			remove();
		}else{
			preserve();
		}
	}else{
		if(isMcuVisible == 0){
			remove();
		}else{
			preserve();
		}
	}
}



extern "C" __global__
void kernel_eval_showCacheVR(
	uint32_t* toDecode,
	HashMap decodedMcuMap,
	uint32_t first,
	uint32_t count
) {
	auto grid = cg::this_grid();

	// if(grid.thread_rank() == 0){
	// 	// printf("test");
	// 	printf("%d \n", first);
	// }

	// return;

	if(grid.thread_rank() >= count) return;

	uint32_t i = first + grid.thread_rank();

	uint32_t key = toDecode[i];

	uint32_t value = 0;
	int location = 0;
	bool exists = decodedMcuMap.get(key, &value, &location);

	// if(i == 48'000){
	// 	// printf("[dbg]: key: %u, location: %d, value: %u \n", key, location, value);
	// 	// printf("decodedMcuMap.entries[%u] = %u | %u; \n", location, key, newValue);
	// }

	// if(grid.thread_rank() == 0) printf("i: %u \n", i);

	if(exists){
		// uint64_t flag = 0b1111'1111;
		uint64_t flag = 0xff;
		uint64_t slot = value & 0x00ffffff;

		uint64_t newValue = (flag << 24) | slot;
		uint64_t entry = (uint64_t(key) << 32) | newValue;
		decodedMcuMap.entries[location] = entry;
	}
}

extern "C" __global__
void kernel_dummy(
	uint32_t* var
) {
	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();

	if(grid.thread_rank() != 0) return;

	*var = 123;
}