#version 120
attribute vec4 attr_Position;
attribute vec2 attr_TexCoord0;
attribute vec4 attr_Color;
centroid varying vec2 var_TexCoords;
varying vec4 var_Color;
uniform vec4 u_LightDirs[2];
invariant gl_Position;

void main() {
    var_TexCoords = attr_TexCoord0;
    var_Color = attr_Color + u_LightDirs[0] * 0.0;
    gl_Position = gl_ModelViewProjectionMatrix * attr_Position;
}
