#pragma once

#include <functional>

#include "SceneNode.h"

using namespace std;

constexpr int MAX_SCENENODES = 100'000;

struct Scene {

	shared_ptr<SceneNode> root = nullptr;
	shared_ptr<SceneNode> world = nullptr;
	shared_ptr<SceneNode> vr = nullptr;
	shared_ptr<SceneNode> placingContainer = nullptr;
	 
	// Extra transformation of the scene.
	// Meant for VR, where we'd like to move, rotate and even scale the scene as part of the navigation.
	// mat4 transform = mat4(1.0f);

	Scene() {
		root = make_shared<SceneNode>("root");
		world = make_shared<SceneNode>("world");
		vr = make_shared<SceneNode>("vr");
		placingContainer = make_shared<SceneNode>("placingContainer");

		root->children.push_back(world);
		root->children.push_back(vr);
		root->children.push_back(placingContainer);
	}

	void process(string name, function<void(SceneNode*)> f){

		SceneNode* node = this->find(name);

		if(node){
			f(node);
		}
	}

	SceneNode* find(string name){

		SceneNode* found = nullptr;

		root->traverse([&](SceneNode* node){
			if(node->name == name){
				found = node;
			}
		});

		return found;
	}

	SceneNode* find(const function<bool(SceneNode*)>& callback){

		SceneNode* found = nullptr;

		root->traverse([&](SceneNode* node){
			if(callback(node)){
				found = node;
			}
		});

		return found;
	}

	void erase(const function<bool(SceneNode*)>& match){

		root->traverse([&](SceneNode* node){
			
			vector<int> childrenToErase;
			
			for(int i = 0; i < node->children.size(); i++){
				
				SceneNode* child = node->children[i].get();

				if(match(child)){
					childrenToErase.push_back(i);
				}
			}

			for(int i = childrenToErase.size() - 1; i >= 0; i--){
				int childIndex = childrenToErase[i];
				node->children.erase(node->children.begin() + childIndex);
			}

		});

	}

	void remove(shared_ptr<SceneNode> node){
		int tmpIndex = -1;
		for(int i = 0; i < root->children.size(); i++){
			if(root->children[i] == node){
				tmpIndex = i;
			}
		}

		if(tmpIndex != -1){
			root->children.erase(root->children.begin() + tmpIndex);
		}
	}

	template<typename T>
	void process(const function<void(T*)>& callback){
		this->forEach<T>(callback);
	}

	template<typename T>
	void forEach(const function<void(T*)>& callback){

		root->traverse([&](SceneNode* node){
			if(dynamic_cast<T*>(node) != nullptr){

				T* sn = dynamic_cast<T*>(node);
				callback(sn);
			}
		});
	}

	void updateTransformations(){

		double t_start = now();

		function<void(SceneNode*, SceneNode*)> traverse;

		traverse = [&](SceneNode* parent, SceneNode* node){
			if (parent) {
				mat4 transform = parent->transform_global * node->transform;
				node->transform_global = transform;
			} else {
				node->transform_global = node->transform;
			}

			for(auto child : node->children){
				traverse(node, child.get());
			}

		};

		traverse(nullptr, root.get());

		double t_end = now();
		double nanos = t_end - t_start;
		double millies = nanos / 1'000'000.0;
	}

	void deselectAllNodes(){
		process<SceneNode>([&](SceneNode* node){
			node->selected = false;
		});
	}

};