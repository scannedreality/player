#version 450

layout (location = 0) in vec2 in_position;

layout (set = 0, binding = 0, std140) uniform PerFrameUBO {
  mat4 modelViewProjection;
} ubo;

void main() {
  gl_Position = ubo.modelViewProjection * vec4(in_position.xy, 0.0, 1.0);
}
