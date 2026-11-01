#version 450
#extension GL_KHR_shader_subgroup_arithmetic : require
layout(location=0) out vec4 o;
void main(){ float v = subgroupAdd(gl_FragCoord.x); o = vec4(v); }
