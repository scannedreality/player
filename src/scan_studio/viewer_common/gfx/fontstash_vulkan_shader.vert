#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../common/sRGB.glsl"

layout (set = 1, binding = 0, std140) uniform PerFrameUBO {
  mat4 modelViewProjection;
} ubo;

layout (location = 0) in vec2 in_position;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 vColor;
layout (location = 1) out vec2 vTexcoord;

void main() {
  vColor = vec4(SRGBToLinear(in_color.rgb), in_color.a);
  vTexcoord = in_texcoord;
  gl_Position = ubo.modelViewProjection * vec4(in_position.xy, 0.0, 1.0);
}
