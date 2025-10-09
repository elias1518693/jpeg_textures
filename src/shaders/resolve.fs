#version 460 core

#extension GL_NV_gpu_shader5 : enable
#extension GL_ARB_bindless_texture : require

uniform sampler2D tex_uv;
uniform sampler2D tex_id_mip;
uniform sampler2D* u_textures;

in vec2 o_uv;

layout(location = 0) out vec4 out_color;

void main()
{
	// return;

	vec4 uv_encoded = texture(tex_uv, o_uv);
	vec4 id_encoded = texture(tex_id_mip, o_uv);

	float u = 0;
	u += int(uv_encoded.x * 256.0) << 0;
	u += int(uv_encoded.y * 256.0) << 8;
	u = u / 65536.0;

	float v = 0;
	v += int(uv_encoded.z * 256.0) << 0;
	v += int(uv_encoded.w * 256.0) << 8;
	v = v / 65536.0;

	int texID = 0;
	texID += int(id_encoded.z * 256.0) << 0;
	texID += int(id_encoded.w * 256.0) << 8;

	int mipLevel = 0;
	mipLevel += int(id_encoded.y * 256.0) << 0;
	// mipLevel += int(id_encoded.y * 256.0) << 8;

	sampler2D tex = u_textures[texID];
	// vec4 texColor = texture(tex, vec2(u, v));
	vec4 texColor = textureLod(tex, vec2(u, v), mipLevel);

	// out_color = texColor * 0.001 + vec4(1.0, 0.0, 0.0, 1.0);
	out_color = texColor;
}
