
#pragma once

#include <functional>
#include <vector>
#include <string>

#define GLEW_STATIC

#include "GL/glew.h"
#include "GLFW/glfw3.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "implot_internal.h"
#include "ImGuizmo.h"

#include "glm/common.hpp"
#include "glm/matrix.hpp"
#include <glm/gtx/transform.hpp>

#include "unsuck.hpp"
#include "OrbitControls.h"

// using namespace std;
using glm::dvec3;
using glm::dvec4;
using glm::vec3;
using glm::vec4;
using glm::dmat4;

struct GLRenderer;

// ScrollingBuffer from ImPlot implot_demo.cpp.
// MIT License
// url: https://github.com/epezent/implot
struct ScrollingBuffer {
	int MaxSize;
	int Offset;
	ImVector<ImVec2> Data;
	ScrollingBuffer() {
		MaxSize = 2000;
		Offset = 0;
		Data.reserve(MaxSize);
	}
	void AddPoint(float x, float y) {
		if (Data.size() < MaxSize)
			Data.push_back(ImVec2(x, y));
		else {
			Data[Offset] = ImVec2(x, y);
			Offset = (Offset + 1) % MaxSize;
		}
	}
	void Erase() {
		if (Data.size() > 0) {
			Data.shrink(0);
			Offset = 0;
		}
	}
};

struct GLBuffer{

	GLuint handle = -1;
	int64_t size = 0;

};

struct GLTexture {
	GLuint handle = -1;
	GLuint64 handle_bindless = 0;
	GLuint colorType = -1;
	int width = 0;
	int height = 0;
	int64_t version = 0;
	int64_t ID = 0;
	inline static int64_t idcounter = 0;
	string label = "";

	void setSize(int width, int height);

};

struct Framebuffer {

	vector<shared_ptr<GLTexture>> colorAttachments;
	shared_ptr<GLTexture> depth;
	GLuint handle = -1;
	string label = "";

	int width = 0;
	int height = 0;
	int64_t version = 0;

	Framebuffer() {
		
	}

	static shared_ptr<Framebuffer> create(string label);

	void setSize(int width, int height) {


		bool needsResize = this->width != width || this->height != height;

		if (needsResize) {

			// COLOR
			for (int i = 0; i < this->colorAttachments.size(); i++) {
				auto& attachment = this->colorAttachments[i];
				attachment->setSize(width, height);
				glNamedFramebufferTexture(this->handle, GL_COLOR_ATTACHMENT0 + i, attachment->handle, 0);
			}

			{ // DEPTH
				this->depth->setSize(width, height);
				glNamedFramebufferTexture(this->handle, GL_DEPTH_ATTACHMENT, this->depth->handle, 0);
			}
			
			this->width = width;
			this->height = height;

			version++;
		}
		

	}

};

struct View{
	dmat4 view;
	dmat4 proj;
	shared_ptr<Framebuffer> framebuffer = nullptr;
};

struct Camera{

	glm::dvec3 position;
	glm::dmat4 rotation;

	glm::dmat4 world;
	glm::dmat4 view;
	glm::dmat4 proj;

	double aspect = 1.0;
	double fovy = 60.0;
	double near = 0.01;
	double far = 2'000'000.0;
	int width = 128;
	int height = 128;

	Camera(){

	}

	void setSize(int width, int height){
		this->width = width;
		this->height = height;
		this->aspect = double(width) / double(height);
	}

	void update(){
		view =  glm::inverse(world);

		double pi = glm::pi<double>();
		
		//proj = glm::perspective(pi * fovy / 180.0, aspect, near, far);
		proj = Camera::createProjectionMatrix(near, pi * fovy / 180.0, aspect);
	}

	vec3 getRayDir(float u, float v){
		// prepare rays
		vec3 origin = getPosition();

		vec4 dir_00_projspace = vec4{-1.0f, -1.0f, 1.0f, 1.0f};
		vec4 dir_01_projspace = vec4{-1.0f,  1.0f, 1.0f, 1.0f};
		vec4 dir_10_projspace = vec4{ 1.0f, -1.0f, 1.0f, 1.0f};
		vec4 dir_11_projspace = vec4{ 1.0f,  1.0f, 1.0f, 1.0f};

		float right = 1.0f / proj[0][0];
		float up = 1.0f / proj[1][1];
		vec4 dir_00_worldspace = inverse(view) * vec4(-right, -up, -1.0f, 1.0f);
		vec4 dir_01_worldspace = inverse(view) * vec4(-right,  up, -1.0f, 1.0f);
		vec4 dir_10_worldspace = inverse(view) * vec4( right, -up, -1.0f, 1.0f);
		vec4 dir_11_worldspace = inverse(view) * vec4( right,  up, -1.0f, 1.0f);

		auto getRayDir = [&](float u, float v){
			float A_00 = (1.0f - u) * (1.0f - v);
			float A_01 = (1.0f - u) *         v;
			float A_10 =         u  * (1.0f - v);
			float A_11 =         u  *         v;

			vec3 dir = (
				A_00 * dir_00_worldspace + 
				A_01 * dir_01_worldspace + 
				A_10 * dir_10_worldspace + 
				A_11 * dir_11_worldspace - vec4(origin, 1.0));
			dir = normalize(dir);

			return dir;
		};

		return getRayDir(u, v);
	}

	vec3 getPosition(){
		return dvec3(inverse(view) * dvec4(0.0, 0.0, 0.0, 1.0));
	}

	inline static glm::mat4 createProjectionMatrix(float near, float fovy, float aspect){

		float f = 1.0f / tan(fovy / 2.0f);

		glm::mat4 proj = glm::mat4(
			f / aspect , 0.0         , 0.0    ,  0.0,
			0.0        , f           , 0.0    ,  0.0,
			0.0        , 0.0         , 0.0    , -1.0,
			0.0        , 0.0         , near   ,  0.0
		);

		return proj;
	}


};



struct GLRenderer{

	inline static GLFWwindow* window = nullptr;
	inline static double fps = 0.0;
	inline static double timeSinceLastFrame = 0.0;
	inline static int64_t frameCount = 0;
	
	inline static shared_ptr<Camera> camera = nullptr;
	// shared_ptr<OrbitControls> controls = nullptr;
	// inline static shared_ptr<ProxyControls> controls = nullptr;
	inline static vector<shared_ptr<GLTexture>> textures;

	inline static bool vrEnabled = false;
	
	inline static View view;
	inline static GLuint dummyVao;

	inline static vector<function<void(vector<string>)>> fileDropListeners;

	inline static int width = 0;
	inline static int height = 0;
	inline static string selectedMethod = "";

	static void init();

	static shared_ptr<GLTexture> createTexture(int width, int height, GLuint colorType, string label);

	static shared_ptr<Framebuffer> createFramebuffer(int width, int height);

	inline static GLBuffer createBuffer(int64_t size){
		GLuint handle;
		glCreateBuffers(1, &handle);
		glNamedBufferStorage(handle, size, 0, GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT);

		GLBuffer buffer;
		buffer.handle = handle;
		buffer.size = size;

		return buffer;
	}

	inline static GLBuffer createSparseBuffer(int64_t size){
		GLuint handle;
		glCreateBuffers(1, &handle);
		glNamedBufferStorage(handle, size, 0, GL_DYNAMIC_STORAGE_BIT | GL_SPARSE_STORAGE_BIT_ARB );

		GLBuffer buffer;
		buffer.handle = handle;
		buffer.size = size;

		return buffer;
	}

	inline static GLBuffer createUniformBuffer(int64_t size){
		GLuint handle;
		glCreateBuffers(1, &handle);
		glNamedBufferStorage(handle, size, 0, GL_DYNAMIC_STORAGE_BIT );

		GLBuffer buffer;
		buffer.handle = handle;
		buffer.size = size;

		return buffer;
	}

	inline static shared_ptr<Buffer> readBuffer(GLBuffer glBuffer, uint32_t offset, uint32_t size){

		auto target = make_shared<Buffer>(size);

		glGetNamedBufferSubData(glBuffer.handle, offset, size, target->data);

		return target;
	}

	static void loop(
		function<void(void)> update,
		function<void(void)> render,
		function<void(void)> postFrame
	);

	inline static void onFileDrop(function<void(vector<string>)> callback){
		GLRenderer::fileDropListeners.push_back(callback);
	}

	private: 
	GLRenderer(){}
};

void installDragEnterHandler(GLFWwindow* window, function<void(string)> callback);
void installDragDropHandler(GLFWwindow* window, function<void(string)> callback);
void installDragOverHandler(GLFWwindow* window, function<void(int32_t, int32_t)> callback);