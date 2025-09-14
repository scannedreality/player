#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../../common/sRGB.glsl"

layout (set = 0, binding = 2) uniform sampler2D textureLuma;
layout (set = 0, binding = 3) uniform sampler2D textureChromaU;
layout (set = 0, binding = 4) uniform sampler2D textureChromaV;
layout (push_constant) uniform PushConstant {
  vec2 textureSize;
  float useRGBTexture;
} pc;

layout (location = 0) centroid in vec2 vTexcoord;
#ifdef USE_ALPHA_BLENDING
layout (location = 1) centroid in float vVertexAlpha;
#endif

layout (location = 0) out vec4 outColor;

vec3 SampleLinearRGB(const in ivec2 xy) {
  float luma = texelFetch(textureLuma, xy, /*lod*/ 0).x;
  float chromaU = texelFetch(textureChromaU, xy / 2, /*lod*/ 0).x;
  float chromaV = texelFetch(textureChromaV, xy / 2, /*lod*/ 0).x;
  
  luma -= 16. / 255.;
  chromaU -= 128. / 255.;
  chromaV -= 128. / 255.;
  
  vec3 srgb = clamp(vec3(
      1.164 * luma                   + 1.596 * chromaV,
      1.164 * luma - 0.392 * chromaU - 0.813 * chromaV,
      1.164 * luma + 2.017 * chromaU                  ), 0., 1.);
  
  return SRGBToLinear(srgb);
}

void main() {
  // Simple bilinear interpolation only (in linear RGB space, after converting the four input colors from YUV to linear RGB).
  // TODO: Can we use mip-mapping and anisotropic filtering as well? We need to make sure that the texture is suited for that.
  
  if (pc.useRGBTexture > 0.5f) {
    outColor =
    #ifdef USE_ALPHA_BLENDING
        vVertexAlpha *
    #endif
        vec4(textureLod(textureLuma, vTexcoord, 0.0).rgb, 1.0);
    return;
  }
  
  vec2 xy = pc.textureSize * vTexcoord - vec2(0.5, 0.5);
  
  ivec2 baseXY = ivec2(xy);
  vec2 frac = fract(xy);
  
  vec3 topLeft = SampleLinearRGB(baseXY);
  vec3 topRight = SampleLinearRGB(ivec2(baseXY.x + 1, baseXY.y));
  vec3 bottomLeft = SampleLinearRGB(ivec2(baseXY.x, baseXY.y + 1));
  vec3 bottomRight = SampleLinearRGB(ivec2(baseXY.x + 1, baseXY.y + 1));
  
  // Bilinear interpolation.
  float topLeftWeight     = (1.0 - frac.x) * (1.0 - frac.y);
  float topRightWeight    =        frac.x  * (1.0 - frac.y);
  float bottomLeftWeight  = (1.0 - frac.x) *        frac.y;
  float bottomRightWeight =        frac.x  *        frac.y;
  outColor =
#ifdef USE_ALPHA_BLENDING
      vVertexAlpha *
#endif
      vec4(
      topLeftWeight * topLeft +
      topRightWeight * topRight +
      bottomLeftWeight * bottomLeft +
      bottomRightWeight * bottomRight,
      1.0);
}
