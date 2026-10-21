#version 150
#ifdef GL_ES
precision highp float;
#endif
#define LIGHT_COUNT \
    2

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
noperspective out vec2 v_texCoords;
layout(location = 2) smooth out vec2 v_lightUV;
flat out int v_lightIndex;
uniform highp mat4 u_projTrans;
uniform vec3 u_lightColors[LIGHT_COUNT];
const float u_alphaScale = 1.0;

void main() {
    v_texCoords = a_texCoord;
    v_lightUV = a_texCoord;
    v_lightIndex = 0;
    gl_Position = u_projTrans * vec4(a_position, 0.0, 1.0);
}
