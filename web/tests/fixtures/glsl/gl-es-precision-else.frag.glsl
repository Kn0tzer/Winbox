#version 300 es
precision highp float;
precision highp int;
out vec4 fragColor;
#ifdef GL_ES
precision mediump float;
precision mediump int;
#else
#define lowp
#define mediump
#define highp
#endif
in vec2 v_texCoords;
uniform sampler2D u_texture0;
void main(){
    fragColor = texture(u_texture0, v_texCoords);
}
