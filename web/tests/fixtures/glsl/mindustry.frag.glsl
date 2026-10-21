#version 150
#ifdef GL_ES
precision mediump float;
#endif
#define LIGHT_COUNT \
    2

layout(location = 0) out vec4 fragColor;
noperspective in vec2 v_texCoords;
layout(location = 2) smooth in vec2 v_lightUV;
flat in int v_lightIndex;
uniform sampler2D u_texture;
uniform vec3 u_lightColors[LIGHT_COUNT];
const float u_alphaScale = 1.0;

void main() {
    vec3 color = texture(u_texture, v_texCoords).rgb * u_lightColors[v_lightIndex];
    fragColor = vec4(color, u_alphaScale);
}
