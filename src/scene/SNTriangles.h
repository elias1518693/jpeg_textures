#pragma once

#include <string>
#include <vector>

#include "SceneNode.h"
#include "./kernels/HostDeviceInterface.h"

using std::string;
using std::vector;
using glm::ivec2;

struct SNTriangles : public SceneNode{

	
	GLuint ssbo_position = -1;
	GLuint ssbo_uv = -1;
	GLuint64 ptr_ssbo_position = -1;
	GLuint64 ptr_ssbo_uv = -1;

	uint32_t numTriangles = 0;

	uint8_t textureIndex;
	shared_ptr<GLTexture> texture;
	
	SNTriangles(string name) : SceneNode(name){
		
	}

	void set(vector<vec3>& positions, vector<vec2>& uvs){

		{ // POSITION
			uint64_t numBytes = positions.size() * sizeof(glm::vec3);
			glCreateBuffers(1, &ssbo_position);
			glNamedBufferStorage(ssbo_position, numBytes, positions.data(), GL_DYNAMIC_STORAGE_BIT);
			glGetNamedBufferParameterui64vNV(ssbo_position, GL_BUFFER_GPU_ADDRESS_NV, &ptr_ssbo_position);
			glMakeNamedBufferResidentNV(ssbo_position, GL_READ_ONLY);
		}

		{ // UV
			uint64_t numBytes = uvs.size() * sizeof(glm::vec2);
			glCreateBuffers(1, &ssbo_uv);
			glNamedBufferStorage(ssbo_uv, numBytes, uvs.data(), GL_DYNAMIC_STORAGE_BIT);
			glGetNamedBufferParameterui64vNV(ssbo_uv, GL_BUFFER_GPU_ADDRESS_NV, &ptr_ssbo_uv);
			glMakeNamedBufferResidentNV(ssbo_uv, GL_READ_ONLY);
		}

		numTriangles = positions.size() / 3;

		for(int i = 0; i < positions.size(); i++){
			vec3 pos = positions[i];
			aabb.min.x = min(aabb.min.x, pos.x);
			aabb.min.y = min(aabb.min.y, pos.y);
			aabb.min.z = min(aabb.min.z, pos.z);
			aabb.max.x = max(aabb.max.x, pos.x);
			aabb.max.y = max(aabb.max.y, pos.y);
			aabb.max.z = max(aabb.max.z, pos.z);
		}
	}

	void setTexture(ivec2 size, void* data){

		string label = format("{}_texture", name);
		texture = GLRenderer::createTexture(size.x, size.y, GL_RGBA8, label);

		glTextureParameteri(texture->handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture->handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture->handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(texture->handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		int maxLevel = log2(max(texture->width, texture->height));
		int level = min(maxLevel, 8);
		glTextureStorage2D(texture->handle, maxLevel, texture->colorType, texture->width, texture->height);
		glTextureSubImage2D(texture->handle, 0, 0, 0, texture->width, texture->height, GL_RGBA, GL_UNSIGNED_BYTE, data);

		glGenerateTextureMipmap(texture->handle);
		
		texture->handle_bindless = glGetTextureHandleARB(texture->handle);
		glMakeTextureHandleResidentARB(texture->handle_bindless);
	}

	uint64_t getGpuMemoryUsage(){
		return 0;
	}

};