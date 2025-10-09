#pragma once

#include <string>
#include <format>
#include "cuda.h"
#include "cuda_runtime.h"

#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/matrix.hpp>
#include <glm/gtx/transform.hpp>

#include "GLRenderer.h"
#include "./kernels/HostDeviceInterface.h"

using namespace std;
using glm::ivec2;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::mat4;
using glm::quat;
//using glm::dot;
//using glm::transpose;
//using glm::inverse;


struct SceneNode{
	
	string name;

	// local transform of each matrix
	mat4 transform = mat4(1.0f);

	// global transform with transformations of parents applied to it.
	// updatated via scene.updateTransformations()
	mat4 transform_global = mat4(1.0f);


	bool visible = true;   // visible as in rendered
	bool locked = false;
	bool hidden = false;   // hidden from layers user interface
	bool selected = false;
	bool hovered = false;
	shared_ptr<GLTexture> thumbnail = nullptr;
	bool dirty = false;
	
	Box3 aabb;
	CUdeviceptr cptr_aabb = 0;
	void* h_aabb_pinned = nullptr;
	
	union{
		uint32_t color = 0x000000ff;
		uint8_t rgba[4];
	};

	vector<shared_ptr<SceneNode>> children;

	SceneNode(){
		static uint32_t counter = 0;

		this->name = format("node_{}", counter);

		counter++;

	}
	
	SceneNode(string name){
		this->name = name;
	}

	void traverse(function<void(SceneNode*)> callback){
		callback(this);

		for(auto child : children){
			child->traverse(callback);
		}
	}

	virtual string toString(){
		return "SceneNode";
	}

	virtual uint64_t getGpuMemoryUsage(){
		return 0;
	}

	virtual Box3 getBoundingBox(){
		Box3 box;
		return box;
	}

	SceneNode* find(string name){

		SceneNode* found = nullptr;

		this->traverse([&](SceneNode* node){
			if(node->name == name){
				found = node;
			}
		});

		return found;
	}

	void remove(SceneNode* toRemove){
		for(int i = 0; i < children.size(); i++){
			if(children[i].get() == toRemove) {
				children.erase(children.begin() + i);
				return;
			}
		}
	}

};