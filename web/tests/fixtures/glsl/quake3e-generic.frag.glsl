#version 120
centroid varying vec2 var_TexCoords;
varying vec4 var_Color;
uniform sampler2D u_DiffuseMap;
uniform vec4 u_Tint[2];

void main() {
    gl_FragData[0] = texture2D(u_DiffuseMap, var_TexCoords) * var_Color * u_Tint[0];
}
