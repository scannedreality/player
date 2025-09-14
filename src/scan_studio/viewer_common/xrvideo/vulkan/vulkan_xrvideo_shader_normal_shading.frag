#version 450
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec3 vViewPos;

layout (location = 0) out vec4 outColor;

void main() {
  vec3 fdx = dFdx(vViewPos);
  vec3 fdy = dFdy(vViewPos);
  vec3 normal = normalize(cross(fdx, fdy));
  
  outColor = vec4(0.5 * (normal + vec3(1, 1, 1)), 1.0);
}
