#include <metal_stdlib>

#include "../../common/sRGB.metal"

using namespace metal;

#include "fontstash_metal_shader.h"

struct RasterizerData {
  float4 position [[position]];
  float4 color;
  float2 texcoord;
};

vertex RasterizerData
Fontstash_VertexMain(
    uint vertexID [[vertex_id]],
    device const Fontstash_Vertex* vertices [[buffer(Fontstash_VertexInputIndex_vertices)]],
    constant Fontstash_Vertex_InstanceData& instanceData [[buffer(Fontstash_VertexInputIndex_instanceData)]]) {
  const device Fontstash_Vertex& v = vertices[vertexID];
  RasterizerData out;
  
  out.color = float4(SRGBToLinear(float3(v.color.rgb) * float3(1 / 255.f)), v.color.a / 255.f);
  out.texcoord = v.texcoord;
  out.position = instanceData.modelViewProjection * float4(v.position, 0.f, 1.f);
  
  out.position.y = -1.f * out.position.y;  // flip y for Metal
  return out;
}

fragment float4 Fontstash_FragmentMain(
    RasterizerData in [[stage_in]],
    texture2d<half, access::sample> fontTexture [[texture(Fontstash_FragmentTextureInputIndex_fontTexture)]]) {
  constexpr sampler fontTextureSampler(filter::linear);
  const float alpha = fontTexture.sample(fontTextureSampler, in.texcoord).r;
  return in.color * float4(alpha, alpha, alpha, alpha);
}
