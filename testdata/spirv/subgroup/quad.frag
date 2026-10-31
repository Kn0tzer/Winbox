#version 450
#extension GL_KHR_shader_subgroup_quad : require
layout(location=0) out vec4 o;
void main(){ float v = subgroupQuadBroadcast(gl_FragCoord.x, 1u); o = vec4(v); }
