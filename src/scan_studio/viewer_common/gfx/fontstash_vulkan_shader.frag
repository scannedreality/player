#version 450

layout (set = 0, binding = 0) uniform sampler2D fontTexture;

layout (location = 0) in vec4 vColor;
layout (location = 1) in vec2 vTexcoord;

layout (location = 0) out vec4 outColor;

void main() {
  float alpha = textureLod(fontTexture, vTexcoord, 0.).x;
  outColor = vColor * vec4(alpha, alpha, alpha, alpha);
}
