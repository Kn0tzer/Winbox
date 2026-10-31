#version 450
#extension GL_KHR_shader_subgroup_vote : require
layout(location=0) out vec4 o;
void main(){ o = vec4(subgroupAny(gl_FragCoord.x > 0.5) ? 1.0 : 0.0); }
