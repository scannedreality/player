#ifdef __APPLE__
#include "scan_studio/viewer_common/gfx/shape2d_metal.hpp"

#include "scan_studio/common/sRGB.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/shape2d_shader.hpp"

namespace scan_studio {

Shape2DMetal::~Shape2DMetal() {
  Destroy();
}

bool Shape2DMetal::Initialize(int maxVertices, int maxIndices, int /*viewCount*/, Shape2DShader* /*shader*/, RenderState* renderState) {
  MetalRenderState* metalState = renderState->AsMetalRenderState();
  
  deviceHasUnifiedMemory = metalState->device->hasUnifiedMemory();
  
  const int perFrameResourceCount = metalState->framesInFlightCount;
  const MTL::ResourceOptions storageMode = deviceHasUnifiedMemory ? MTL::ResourceStorageModeShared : MTL::ResourceStorageModeManaged;
  
  vertexBuffers.resize(perFrameResourceCount);
  indexBuffers.resize(perFrameResourceCount);
  
  for (int i = 0; i < perFrameResourceCount; ++ i) {
    vertexBuffers[i] = NS::TransferPtr(metalState->device->newBuffer(maxVertices * sizeof(Shape2D_Vertex), storageMode | MTL::ResourceCPUCacheModeWriteCombined));
    if (!vertexBuffers[i]) { LOG(ERROR) << "Failed to allocate vertex buffer"; return false; }
    
    indexBuffers[i] = NS::TransferPtr(metalState->device->newBuffer(maxIndices * sizeof(u16), storageMode | MTL::ResourceCPUCacheModeWriteCombined));
    if (!indexBuffers[i]) { LOG(ERROR) << "Failed to allocate index buffer"; return false; }
  }
  
  return true;
}

void Shape2DMetal::Destroy() {
  vertexBuffers.clear();
  indexBuffers.clear();
}

void Shape2DMetal::SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* /*renderState*/) {
  const u32 verticesSize = vertexCount * sizeof(Shape2D_Vertex);
  const u32 indicesSize = indexCount * sizeof(u16);
  
  currentPerFrameResourceIndex = (currentPerFrameResourceIndex + 1) % vertexBuffers.size();
  auto& vertexBuffer = vertexBuffers[currentPerFrameResourceIndex];
  auto& indexBuffer = indexBuffers[currentPerFrameResourceIndex];
  
  memcpy(vertexBuffer->contents(), vertices, verticesSize);
  memcpy(indexBuffer->contents(), indices, indicesSize);
  
  if (!deviceHasUnifiedMemory) {
    vertexBuffer->didModifyRange(NS::Range::Make(0, verticesSize));
    indexBuffer->didModifyRange(NS::Range::Make(0, indicesSize));
  }
  
  this->indexCount = indexCount;
}

void Shape2DMetal::RenderView(const Eigen::Vector4f& color, int /*viewIndex*/, Shape2DShader* shader, RenderState* renderState) {
  MetalRenderState* metalState = renderState->AsMetalRenderState();
  auto& encoder = metalState->renderCmdEncoder;
  Shape2DShaderMetal* shaderMetal = shader->GetMetalImpl();
  
  auto& vertexBuffer = vertexBuffers[currentPerFrameResourceIndex];
  auto& indexBuffer = indexBuffers[currentPerFrameResourceIndex];
  
  encoder->setCullMode(MTL::CullModeNone);
  encoder->setDepthStencilState(shaderMetal->depthStencilState.get());
  
  encoder->setRenderPipelineState(shaderMetal->renderPipelineState.get());
  
  encoder->setVertexBuffer(vertexBuffer.get(), /*offset*/ 0, Shape2D_VertexInputIndex_vertices);
  
  encoder->setVertexBytes(&vertexInstanceData, sizeof(vertexInstanceData), Shape2D_VertexInputIndex_instanceData);
  
  const Eigen::Vector3f linearColor = SRGBToLinear(Eigen::Vector3f(color.topRows<3>()));
  fragmentInstanceData.color = simd_make_float4(linearColor(0), linearColor(1), linearColor(2), color(3));
  encoder->setFragmentBytes(&fragmentInstanceData, sizeof(fragmentInstanceData), Shape2D_FragmentInputIndex_instanceData);
  
  encoder->drawIndexedPrimitives(
      MTL::PrimitiveType::PrimitiveTypeTriangle,
      indexCount, MTL::IndexTypeUInt16, indexBuffer.get(), /*indexBufferOffset*/ 0);
}

void Shape2DMetal::SetModelViewProjection(int /*viewIndex*/, const float* columnMajorModelViewProjectionData) {
  memcpy(&vertexInstanceData.modelViewProjection, columnMajorModelViewProjectionData, sizeof(vertexInstanceData.modelViewProjection));
}

}

#endif
