#version 460 core

#extension GL_NV_gpu_shader5 : enable
#extension GL_NV_shader_buffer_load : require

uniform mat4 u_viewProj;

struct Node{
	mat4 world;
	float* position;
	vec2* uv;
	int32_t textureIndex;
	int32_t width;
	int32_t height;
	int32_t padding0;
};

uniform Node* u_nodes;

// Output to fragment shader
out vec2 o_uv;
flat out int o_texID;
flat out int o_width;
flat out int o_height;

void main(){

	Node node = u_nodes[gl_DrawID];
	
	vec3 pos = vec3(
		node.position[3 * gl_VertexID + 0],
		node.position[3 * gl_VertexID + 1],
		node.position[3 * gl_VertexID + 2]
	);

	{ // Some hot-reloadable mesh transformations
	
		// flip y and z
		// pos = vec3(pos.x, pos.z, -pos.y);

		// other stuff
		// pos = pos * 0.1f;
		// pos += vec3(59.0, -28.0, 10.0);
		// pos = vec3(-pos.y, pos.x, pos.z);
	}

	gl_Position = u_viewProj * node.world * vec4(pos, 1.0f);
	o_texID     = node.textureIndex;
	o_uv        = node.uv[gl_VertexID];
	o_width     = node.width;
	o_height    = node.height;
}