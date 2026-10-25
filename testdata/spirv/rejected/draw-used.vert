#version 450
#extension GL_ARB_shader_draw_parameters : require
layout(location = 0) in vec4 inPos;
void main() {
  gl_Position = inPos + vec4(float(gl_BaseVertexARB) + float(gl_DrawIDARB), 0.0, 0.0, 0.0);
}
