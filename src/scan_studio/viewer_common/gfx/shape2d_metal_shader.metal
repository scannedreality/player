#include <metal_stdlib>

using namespace metal;

#include "shape2d_metal_shader.h"

struct RasterizerData {
  float4 position [[position]];
};

vertex RasterizerData
Shape2D_VertexMain(
    uint vertexID [[vertex_id]],
    device const Shape2D_Vertex* vertices [[buffer(Shape2D_VertexInputIndex_vertices)]],
    constant Shape2D_Vertex_InstanceData& instanceData [[buffer(Shape2D_VertexInputIndex_instanceData)]]) {
  const device Shape2D_Vertex& v = vertices[vertexID];
  RasterizerData out;
  
  out.position = instanceData.modelViewProjection * float4(v.position, 0.f, 1.f);
  
  out.position.y = -1.f * out.position.y;  // flip y for Metal
  return out;
}

fragment float4 Shape2D_FragmentMain(
    RasterizerData in [[stage_in]],
    constant Shape2D_Fragment_InstanceData& instanceData [[buffer(Shape2D_FragmentInputIndex_instanceData)]]) {
  return instanceData.color;
}
