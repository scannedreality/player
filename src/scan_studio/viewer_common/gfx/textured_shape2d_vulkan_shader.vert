#version 450

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inTexcoord;

layout (set = 0, binding = 0, std140) uniform PerFrameUBO {
  mat4 modelViewProjection;
} ubo;

layout (location = 0) out vec2 vTexcoord;

void main() {
  vTexcoord = inTexcoord;
  gl_Position = ubo.modelViewProjection * vec4(inPos.xy, 0.0, 1.0);
}
