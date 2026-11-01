#version 450
#extension GL_KHR_shader_subgroup_ballot : require
layout(location=0) out vec4 o;
void main(){ uvec4 b = subgroupBallot(gl_FragCoord.x > 0.5); o = vec4(float(b.x & 1u)); }
