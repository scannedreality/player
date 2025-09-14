#include <metal_stdlib>

#include "../../../common/sRGB.metal"

using namespace metal;

#include "metal_xrvideo_shader.h"

constant constexpr int K = 4;  // must be equal to XRVideoVertex::K

struct RasterizerData {
  float4 position [[position]];
  float2 texcoord [[centroid_perspective]];
};


inline float3 DecodePosition(device const simd_packed_ushort4& encodedPosition, constant const XRVideo_Vertex_InstanceData& instanceData) {
  return instanceData.bboxMin.xyz + instanceData.vertexFactor.xyz * float3(encodedPosition.xyz);
}

vertex RasterizerData
XRVideo_VertexMain(
    uint vertexID [[vertex_id]],
    device const XRVideo_Vertex* vertices [[buffer(XRVideo_VertexInputIndex_vertices)]],
    constant XRVideo_Vertex_InstanceData& instanceData [[buffer(XRVideo_VertexInputIndex_instanceData)]],
    device const float* deformationState [[buffer(XRVideo_VertexInputIndex_deformationState)]]) {
  const device XRVideo_Vertex& v = vertices[vertexID];
  RasterizerData out;
  
  out.texcoord = float2(0.5f / 65536.0f) + float2(1.0f / 65536.0f) * float2(v.texcoord);
  
  float weightsAsFloat[K];
  
  // De-quantize the weights to float
  for (int k = 0; k < K; ++ k) {
    const uchar nodeWeightK = v.nodeWeights[k];
    weightsAsFloat[k] =
        (nodeWeightK == 1) ? (0.5f * (0.5f / 254.f)) :
        ((nodeWeightK == 255) ? (253.75f / 254.f) :
          ((max(nodeWeightK, static_cast<uchar>(1)) - 1) / 254.f));
  }
  
  // Re-normalize the weights
  const float weightFactor = 1.f / (weightsAsFloat[0] + weightsAsFloat[1] + weightsAsFloat[2] + weightsAsFloat[3]);  // assumes K == 4
  
  for (int k = 0; k < K; ++ k) {
    weightsAsFloat[k] *= weightFactor;
  }
  
  // Compute the deformed vertex position
  const float3 originalPosition = DecodePosition(v.position, instanceData);
  float3 deformedPosition = float3(0);
  
  for (int k = 0; k < K; ++ k) {
    const uint baseIdx = 12 * v.nodeIndices[k];
    
    deformedPosition +=
        weightsAsFloat[k] *
        (float3(deformationState[baseIdx + 0] * originalPosition.x + deformationState[baseIdx + 3] * originalPosition.y + deformationState[baseIdx + 6] * originalPosition.z,
                deformationState[baseIdx + 1] * originalPosition.x + deformationState[baseIdx + 4] * originalPosition.y + deformationState[baseIdx + 7] * originalPosition.z,
                deformationState[baseIdx + 2] * originalPosition.x + deformationState[baseIdx + 5] * originalPosition.y + deformationState[baseIdx + 8] * originalPosition.z) +
         float3(deformationState[baseIdx + 9], deformationState[baseIdx + 10], deformationState[baseIdx + 11]));
  }
  
  out.position = instanceData.modelViewProjection * float4(deformedPosition, 1.0);
  
  out.position.y = -1.f * out.position.y;  // flip y for Metal
  return out;
}


inline float3 SampleLinearRGB(
    ushort2 xy,
    texture2d<half, access::read> textureLuma,
    texture2d<half, access::read> textureChromaU,
    texture2d<half, access::read> textureChromaV) {
  float luma = textureLuma.read(xy).x;
  float chromaU = textureChromaU.read(xy / 2).x;
  float chromaV = textureChromaV.read(xy / 2).x;
  
  luma -= 16.f / 255.f;
  chromaU -= 128.f / 255.f;
  chromaV -= 128.f / 255.f;
  
  const float3 srgb = clamp(float3(
      1.164f * luma                    + 1.596f * chromaV,
      1.164f * luma - 0.392f * chromaU - 0.813f * chromaV,
      1.164f * luma + 2.017f * chromaU                   ), 0.f, 1.f);
  
  return SRGBToLinear(srgb);
}

fragment float4 XRVideo_FragmentMain_Linear(
    RasterizerData in [[stage_in]],
    texture2d<half, access::read> textureLuma [[texture(XRVideo_FragmentTextureInputIndex_textureLuma)]],
    texture2d<half, access::read> textureChromaU [[texture(XRVideo_FragmentTextureInputIndex_textureChromaU)]],
    texture2d<half, access::read> textureChromaV [[texture(XRVideo_FragmentTextureInputIndex_textureChromaV)]],
    constant XRVideo_Fragment_InstanceData& instanceData [[buffer(XRVideo_FragmentInputIndex_instanceData)]]) {
  // Simple bilinear interpolation only (in linear RGB space, after converting the four input colors from YUV to linear RGB).
  // TODO: Can we use mip-mapping and anisotropic filtering as well? We need to make sure that the texture is suited for that.
  
  float2 xy = instanceData.textureSize * in.texcoord - float2(0.5f, 0.5f);
  ushort2 baseXY = ushort2(xy);
  float2 frac = fract(xy);
  
  const float3 topLeft = SampleLinearRGB(baseXY, textureLuma, textureChromaU, textureChromaV);
  const float3 topRight = SampleLinearRGB(ushort2(baseXY.x + 1, baseXY.y), textureLuma, textureChromaU, textureChromaV);
  const float3 bottomLeft = SampleLinearRGB(ushort2(baseXY.x, baseXY.y + 1), textureLuma, textureChromaU, textureChromaV);
  const float3 bottomRight = SampleLinearRGB(ushort2(baseXY.x + 1, baseXY.y + 1), textureLuma, textureChromaU, textureChromaV);
  
  // Bilinear interpolation.
  const float topLeftWeight     = (1.0f - frac.x) * (1.0f - frac.y);
  const float topRightWeight    =         frac.x  * (1.0f - frac.y);
  const float bottomLeftWeight  = (1.0f - frac.x) *         frac.y;
  const float bottomRightWeight =         frac.x  *         frac.y;
  return float4(
      topLeftWeight * topLeft +
      topRightWeight * topRight +
      bottomLeftWeight * bottomLeft +
      bottomRightWeight * bottomRight,
      1.0f);
}


inline float3 SampleSRGB(
    ushort2 xy,
    texture2d<half, access::read> textureLuma,
    texture2d<half, access::read> textureChromaU,
    texture2d<half, access::read> textureChromaV) {
  float luma = textureLuma.read(xy).x;
  float chromaU = textureChromaU.read(xy / 2).x;
  float chromaV = textureChromaV.read(xy / 2).x;
  
  luma -= 16.f / 255.f;
  chromaU -= 128.f / 255.f;
  chromaV -= 128.f / 255.f;
  
  const float3 srgb = clamp(float3(
      1.164f * luma                    + 1.596f * chromaV,
      1.164f * luma - 0.392f * chromaU - 0.813f * chromaV,
      1.164f * luma + 2.017f * chromaU                   ), 0.f, 1.f);
  
  return srgb;
}

fragment float4 XRVideo_FragmentMain_sRGB(
    RasterizerData in [[stage_in]],
    texture2d<half, access::read> textureLuma [[texture(XRVideo_FragmentTextureInputIndex_textureLuma)]],
    texture2d<half, access::read> textureChromaU [[texture(XRVideo_FragmentTextureInputIndex_textureChromaU)]],
    texture2d<half, access::read> textureChromaV [[texture(XRVideo_FragmentTextureInputIndex_textureChromaV)]],
    constant XRVideo_Fragment_InstanceData& instanceData [[buffer(XRVideo_FragmentInputIndex_instanceData)]]) {
  // Simple bilinear interpolation only (in linear RGB space, after converting the four input colors from YUV to linear RGB).
  // TODO: Can we use mip-mapping and anisotropic filtering as well? We need to make sure that the texture is suited for that.
  
  float2 xy = instanceData.textureSize * in.texcoord - float2(0.5f, 0.5f);
  ushort2 baseXY = ushort2(xy);
  float2 frac = fract(xy);
  
  const float3 topLeft = SampleSRGB(baseXY, textureLuma, textureChromaU, textureChromaV);
  const float3 topRight = SampleSRGB(ushort2(baseXY.x + 1, baseXY.y), textureLuma, textureChromaU, textureChromaV);
  const float3 bottomLeft = SampleSRGB(ushort2(baseXY.x, baseXY.y + 1), textureLuma, textureChromaU, textureChromaV);
  const float3 bottomRight = SampleSRGB(ushort2(baseXY.x + 1, baseXY.y + 1), textureLuma, textureChromaU, textureChromaV);
  
  // Bilinear interpolation.
  const float topLeftWeight     = (1.0f - frac.x) * (1.0f - frac.y);
  const float topRightWeight    =         frac.x  * (1.0f - frac.y);
  const float bottomLeftWeight  = (1.0f - frac.x) *         frac.y;
  const float bottomRightWeight =         frac.x  *         frac.y;
  return float4(
      topLeftWeight * topLeft +
      topRightWeight * topRight +
      bottomLeftWeight * bottomLeft +
      bottomRightWeight * bottomRight,
      1.0f);
}
