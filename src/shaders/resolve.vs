#version 460 core

#extension GL_NV_gpu_shader5 : enable
#extension GL_NV_shader_buffer_load : require


// Output to fragment shader
out vec2 o_uv;

void main(){


	if(gl_VertexID == 0) gl_Position = vec4(-1.0, -1.0, 0.1, 1.0);
	if(gl_VertexID == 1) gl_Position = vec4( 1.0, -1.0, 0.1, 1.0);
	if(gl_VertexID == 2) gl_Position = vec4( 1.0,  1.0, 0.1, 1.0);
	
	if(gl_VertexID == 3) gl_Position = vec4(-1.0, -1.0, 0.1, 1.0);
	if(gl_VertexID == 4) gl_Position = vec4( 1.0,  1.0, 0.1, 1.0);
	if(gl_VertexID == 5) gl_Position = vec4(-1.0,  1.0, 0.1, 1.0);

	o_uv = (gl_Position.xy + 1.0) / 2.0;

}