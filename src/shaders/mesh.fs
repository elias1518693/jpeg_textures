#version 460 core

#extension GL_NV_gpu_shader5 : enable

uniform bool u_enableMipMapping;

struct Node{
	mat4 world;
	float* position;
	vec2* uv;
	int32_t textureIndex;
	int32_t width;
	int32_t height;
	int32_t padding0;
};


in vec3 FragPos;
in vec2 o_uv;
flat in int o_texID;
flat in int o_width;
flat in int o_height;
layout(location = 0) out vec4 out_uv;
layout(location = 1) out vec4 out_mip;

void main()
{

	vec2 texSize = vec2(o_width, o_height);

	vec2 dx = dFdx(o_uv * texSize);
	vec2 dy = dFdy(o_uv * texSize);

	float mip = 0.0;

	if(u_enableMipMapping){
		mip = 0.5 * log2(max(dot(dx, dx), dot(dy, dy)));
	}

	vec2 uv = vec2(
		clamp(mod(o_uv.x, 1.0f), 0.0, 1.0),
		clamp(mod(o_uv.y, 1.0f), 0.0, 1.0)
	);

	uint32_t uvX = uint32_t(uv.x * 65536.0);
	uint32_t uvY = uint32_t(uv.y * 65536.0);

	uvX = clamp(uvX, 0u, 65535u);
	uvY = clamp(uvY, 0u, 65535u);

	uint32_t packedVal = uvX | (uvY << 16);
	out_uv.x = ((packedVal >>   0) & 0xff) / 255.0f;
	out_uv.y = ((packedVal >>   8) & 0xff) / 255.0f;
	out_uv.z = ((packedVal >>  16) & 0xff) / 255.0f;
	out_uv.w = ((packedVal >>  24) & 0xff) / 255.0f;

	uint32_t uMip = uint32_t(256.0f * mip);
	uint32_t uTexID = o_texID;

	// { // EVALUATION: Apply numerous textures
	// 	int numTextures = 100;
	// 	uTexID = 1 + int(numTextures * uv.x) % numTextures;

	// 	int mcuX = clamp(int(uv.x * 64.0), 0, 63);
	// 	int mcuY = clamp(int(uv.y * 64.0), 0, 63);
	// 	int mcuID = (mcuX + 64 * mcuY);
	// 	uTexID = 1 + (mcuID % numTextures);

	// 	// if(mcuX != 1 || mcuY != 1){
	// 	// 	uTexID = 0;
	// 	// }

	// 	uTexID = 1;
	// }

	uint32_t packedVal2 = uMip | (uTexID << 16);
	out_mip.x = ((packedVal2 >>   0) & 0xff) / 255.0f;
	out_mip.y = ((packedVal2 >>   8) & 0xff) / 255.0f;
	out_mip.z = ((packedVal2 >>  16) & 0xff) / 255.0f;
	out_mip.w = ((packedVal2 >>  24) & 0xff) / 255.0f;

}
