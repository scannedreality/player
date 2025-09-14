#ifdef HAVE_VULKAN

#include "scan_studio/viewer_common/gfx/textured_shape2d_vulkan.hpp"

#include <libvis/vulkan/barrier.h>
#include <libvis/vulkan/command_buffer.h>

#include "scan_studio/common/sRGB.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/textured_shape2d_shader.hpp"

namespace scan_studio {

TexturedShape2DVulkan::~TexturedShape2DVulkan() {
  Destroy();
}

bool TexturedShape2DVulkan::Initialize(int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState) {
  this->viewCount = viewCount;
  
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  TexturedShape2DShaderVulkan* shaderVulkan = shader->GetVulkanImpl();
  
  if (!vertexBuffer.InitializeDeviceLocal(maxVertices * 4 * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, *vulkanState->device)) { return false; }
  vertexBuffer.SetDebugNameIfDebugging("TexturedShape2DVulkan vertexBuffer");
  if (!indexBuffer.InitializeDeviceLocal(maxIndices * sizeof(u16), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, *vulkanState->device)) { return false; }
  indexBuffer.SetDebugNameIfDebugging("TexturedShape2DVulkan indexBuffer");
  
  if (!vertexBufferStaging.InitializeAndMapHostTransferSrc(vulkanState->framesInFlightCount * vertexBuffer.size(), *vulkanState->device)) { return false; }
  vertexBufferStaging.SetDebugNameIfDebugging("TexturedShape2DVulkan vertexBufferStaging");
  if (!indexBufferStaging.InitializeAndMapHostTransferSrc(vulkanState->framesInFlightCount * indexBuffer.size(), *vulkanState->device)) { return false; }
  indexBufferStaging.SetDebugNameIfDebugging("TexturedShape2DVulkan indexBufferStaging");
  
  const u32 viewFrameResourceCount = viewCount * vulkanState->framesInFlightCount;
  
  const VkDeviceSize minAlignment = vulkanState->device->properties().limits.minUniformBufferOffsetAlignment;
  uniformBufferStride = (((uniformBufferSize - 1) / minAlignment) + 1) * minAlignment;
  if (!uniformBuffers.InitializeAndMapUniformBuffer(viewFrameResourceCount * uniformBufferStride, *vulkanState->device)) { return false; }
  uniformBuffers.SetDebugNameIfDebugging("TexturedShape2DVulkan uniformBuffers");
  
  // TODO: It would likely be better to take some descriptors from a set of large pools instead of creating a separate pool for each TexturedShape2DVulkan object
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, /*count*/ viewFrameResourceCount);
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, /*count*/ vulkanState->framesInFlightCount);
  if (!descriptorPool.Initialize(/*max_sets*/ viewFrameResourceCount + vulkanState->framesInFlightCount, *vulkanState->device)) { return false; }
  descriptorPool.SetDebugNameIfDebugging("TexturedShape2DVulkan descriptorPool");
  
  perViewDescriptorSets.resize(viewFrameResourceCount);
  for (int i = 0; i < perViewDescriptorSets.size(); ++ i) {
    auto& descriptorSet = perViewDescriptorSets[i];
    if (!descriptorSet.Initialize(shaderVulkan->perViewDescriptorSetLayout, descriptorPool, *vulkanState->device)) { return false; }
    descriptorSet.SetDebugNameIfDebugging("TexturedShape2DVulkan perViewDescriptorSet");
    
    const VkDescriptorBufferInfo desc = {uniformBuffers, /*offset*/ i * uniformBufferStride, /*range*/ uniformBufferSize};
    descriptorSet.UpdatePerFrameUBO(&desc);
  }
  
  perFrameDescriptorSets.resize(vulkanState->framesInFlightCount);
  for (auto& descriptorSet : perFrameDescriptorSets) {
    if (!descriptorSet.Initialize(shaderVulkan->perFrameDescriptorSetLayout, descriptorPool, *vulkanState->device)) { return false; }
    descriptorSet.SetDebugNameIfDebugging("TexturedShape2DVulkan perFrameDescriptorSet");
  }
  
  return true;
}

void TexturedShape2DVulkan::Destroy() {
  vertexBuffer.Destroy();
  indexBuffer.Destroy();
  
  vertexBufferStaging.Destroy();
  indexBufferStaging.Destroy();
  
  perViewDescriptorSets.clear();
  perFrameDescriptorSets.clear();
  descriptorPool.Destroy();
  uniformBuffers.Destroy();
}

void TexturedShape2DVulkan::SetGeometry(int vertexCount, const void* vertices, int indexCount, const u16* indices, const VkDescriptorImageInfo* texture, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  
  const u32 vertexStagingOffset = vulkanState->frameInFlightIndex * vertexBuffer.size();
  const u32 indexStagingOffset = vulkanState->frameInFlightIndex * indexBuffer.size();
  
  const u32 verticesSize = vertexCount * 4 * sizeof(float);
  const u32 indicesSize = indexCount * sizeof(u16);
  
  memcpy(vertexBufferStaging.data<u8>() + vertexStagingOffset, vertices, verticesSize);
  memcpy(indexBufferStaging.data<u8>() + indexStagingOffset, indices, indicesSize);
  
  this->indexCount = indexCount;
  
  // Wait until the buffers are rendered before starting the next transfer to them
  VulkanMemoryBarrier(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)
      .Submit(*vulkanState->cmdBuf, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  
  vertexBufferStaging.CmdCopyBuffer(*vulkanState->cmdBuf, &vertexBuffer, vertexStagingOffset, /*dstOffset*/ 0, verticesSize);
  indexBufferStaging.CmdCopyBuffer(*vulkanState->cmdBuf, &indexBuffer, indexStagingOffset, /*dstOffset*/ 0, indicesSize);
  
  // Wait until the transfers finished before rendering the buffers
  VulkanMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT)
      .Submit(*vulkanState->cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
  
  const int perFrameDescriptorIndex = vulkanState->frameInFlightIndex;
  perFrameDescriptorSets[perFrameDescriptorIndex].UpdateTexture(texture);
}

void TexturedShape2DVulkan::RenderView(const Eigen::Vector4f& color, int viewIndex, TexturedShape2DShader* shader, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  TexturedShape2DShaderVulkan* shaderVulkan = shader->GetVulkanImpl();
  const auto& api = vulkanState->device->Api();
  
  currentFrameInFlightIndex = vulkanState->frameInFlightIndex;
  const int perViewDescriptorIndex = viewIndex + currentFrameInFlightIndex * viewCount;
  const int perFrameDescriptorIndex = vulkanState->frameInFlightIndex;
  
  api.vkCmdBindPipeline(*vulkanState->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, shaderVulkan->pipeline);
  
  constexpr VkDeviceSize offsets[1] = {0};
  api.vkCmdBindVertexBuffers(*vulkanState->cmdBuf, 0, 1, &vertexBuffer.buffer(), offsets);
  api.vkCmdBindIndexBuffer(*vulkanState->cmdBuf, indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT16);
  const array<VkDescriptorSet, 2> descriptorSets = {
      perViewDescriptorSets[perViewDescriptorIndex].descriptor_set(),
      perFrameDescriptorSets[perFrameDescriptorIndex].descriptor_set()};
  api.vkCmdBindDescriptorSets(
      *vulkanState->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, shaderVulkan->pipelineLayout,
      /*firstSet*/ 0, descriptorSets.size(), descriptorSets.data(), /*dynamicOffsetCount*/ 0, /*pDynamicOffsets*/ nullptr);
  
  const Eigen::Vector4f linearColor = SRGBToLinear(color);
  api.vkCmdPushConstants(*vulkanState->cmdBuf, shaderVulkan->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 * sizeof(float), linearColor.data());
  
  api.vkCmdDrawIndexed(*vulkanState->cmdBuf, indexCount, 1, 0, 0, 0);
}

void TexturedShape2DVulkan::SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) {
  const int descriptorIndex = viewIndex + currentFrameInFlightIndex * viewCount;
  memcpy(uniformBuffers.data<u8>() + descriptorIndex * uniformBufferStride, columnMajorModelViewProjectionData, uniformBufferSize);
}

}

#endif
