#version 450
#extension GL_KHR_shader_subgroup_clustered : require
layout(location=0) out vec4 o;
void main(){ float v = subgroupClusteredAdd(gl_FragCoord.x, 4u); o = vec4(v); }
