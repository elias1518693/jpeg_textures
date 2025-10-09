
#pragma once

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::ivec2;
using glm::ivec3;
using glm::ivec4;
using glm::mat4;

constexpr uint32_t BACKGROUND_COLOR = 0xff332211;
constexpr uint64_t DEFAULT_PIXEL = (uint64_t(0x7f800000) << 32) | BACKGROUND_COLOR;

constexpr int MOD_SHIFT = 1;
constexpr int MOD_CTRL = 2;
constexpr int MOD_ALT = 4;

struct Box3 {
	vec3 min = { Infinity, Infinity, Infinity };
	vec3 max = { -Infinity, -Infinity, -Infinity };

	bool isDefault() {
		return min.x == Infinity && min.y == Infinity && min.z == Infinity && max.x == -Infinity && max.y == -Infinity && max.z == -Infinity;
	}

	bool isEqual(Box3 box, float epsilon) {
		float diff_min = length(box.min - min);
		float diff_max = length(box.max - max);

		if (diff_min >= epsilon) return false;
		if (diff_max >= epsilon) return false;

		return true;
	}
};

struct Texture{
	int width = 0;
	int height = 0;
	uint8_t* data = nullptr;              // regular CUdeviceptr
	cudaSurfaceObject_t surface = -1;     // Cuda-mapping of an OpenGL texture/image
	cudaTextureObject_t cutexture = -1;   // Cuda-mapping of an OpenGL texture
};

struct TriangleData{
	uint32_t count = 0;
	bool visible = true;
	bool locked = false;
	mat4 transform = mat4(1.0f);

	vec3 min;
	vec3 max;

	vec3* position    = nullptr;
	vec2* uv          = nullptr;
	uint32_t* colors  = nullptr;
	uint32_t* indices = nullptr;
};

constexpr int MATERIAL_MODE_COLOR          = 0;
constexpr int MATERIAL_MODE_VERTEXCOLOR    = 1;
constexpr int MATERIAL_MODE_UVS            = 2;
constexpr int MATERIAL_MODE_POSITION       = 3;
constexpr int MATERIAL_MODE_TEXTURED       = 4;

struct TriangleMaterial{
	vec4 color = vec4{1.0f, 0.0f, 0.0f, 1.0f};
	int mode = MATERIAL_MODE_COLOR;
	Texture texture;
	int textureIndex;
	vec2 uv_offset = {0.0f, 0.0f};
	vec2 uv_scale = {1.0f, 1.0f};
};

struct DeviceState{
	int counter;
	uint32_t numMcusDecoded;

	vec3 hovered_pos;
	uint32_t hovered_primitive;
	float hovered_depth;
	vec3 mouseTargetIntersection;
	vec4 hovered_color;

	struct {
		float radius;
		glm::mat4 view;
	} viewdata;
};

struct RenderTarget{
	uint64_t* framebuffer = nullptr;
	uint64_t* indexbuffer = nullptr;
	int width;
	int height;
	mat4 view;
	mat4 proj;
};

struct Uniforms{
	float time;
	float pad;
	mat4 world;
	mat4 camWorld;
	mat4 transform;
	uint32_t frameCount;
	uint32_t fragmentCounter;
	
	bool showUVs;
	bool showMCUs;
	bool showTexID;
	bool showCaching;
	bool showMipLevel;

	bool enableLinearInterpolation;
	bool enableMipMapping;
	bool freezeCache;

	struct {
		bool show;
		ivec2 start;
		ivec2 size;
	} inset;

};

struct Keys{
	int mods;

	uint32_t keyStates[65536];

	bool isCtrlDown(){
		return keyStates[341] != 0;
		// return true;
	}

	bool isAltDown(){
		return keyStates[342] != 0;
	}

	bool isShiftDown(){
		return keyStates[340] != 0;
	}
};

struct KeyEvents{

	struct KeyEvent{
		uint32_t key;
		uint32_t action;
		uint32_t mods;
	};

	int numEvents;
	KeyEvent events[8];
};

struct CommonLaunchArgs{
	Uniforms uniforms;
	DeviceState* state;
	Keys* keys;
	KeyEvents keyEvents;
};
