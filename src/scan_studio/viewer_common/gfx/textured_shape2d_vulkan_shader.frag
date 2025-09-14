#version 450

layout (set = 1, binding = 0) uniform sampler2D texture;
layout (push_constant) uniform PushConstant {
  vec4 color;
} pc;

layout (location = 0) in vec2 vTexcoord;

layout (location = 0) out vec4 outColor;

void main() {
  // Our textures do not have premultiplied alpha applied to them, so multiply their RGB with their alpha value here
  vec4 textureColor = textureLod(texture, vTexcoord, 0.);
  outColor = pc.color * vec4(textureColor.rgb * textureColor.a, textureColor.a);
}
