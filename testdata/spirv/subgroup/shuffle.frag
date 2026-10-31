#version 450
#extension GL_KHR_shader_subgroup_shuffle : require
layout(location=0) out vec4 o;
void main(){ float v = subgroupShuffle(gl_FragCoord.x, 3u); o = vec4(v); }
