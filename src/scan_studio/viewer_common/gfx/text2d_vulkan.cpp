#ifdef HAVE_VULKAN

#include "scan_studio/viewer_common/gfx/text2d_vulkan.hpp"

#include <loguru.hpp>

#include <libvis/vulkan/command_buffer.h>

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/fontstash.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_vulkan.hpp"

namespace scan_studio {

Text2DVulkan::~Text2DVulkan() {
  Destroy();
}

bool Text2DVulkan::Initialize(int viewCount, FontStash* fontStash, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  FontStashShaderVulkan* vulkanShader = fontStash->Shader()->GetVulkanImpl();
  
  this->viewCount = viewCount;
  this->fontStash = fontStash;
  
  const u32 viewFrameResourceCount = viewCount * vulkanState->framesInFlightCount;
  
  // Initialize descriptor pool
  // TODO: It would likely be better to take some descriptors from a set of large pools instead of creating a separate pool for each Shape2DVulkan object
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, viewFrameResourceCount);
  if (!descriptorPool.Initialize(/*max_sets*/ viewFrameResourceCount, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize a descriptor pool"; Destroy(); return false;
  }
  descriptorPool.SetDebugNameIfDebugging("Text2DVulkan descriptorPool");
  
  // Initialize frame descriptors
  const VkDeviceSize minAlignment = vulkanState->device->properties().limits.minUniformBufferOffsetAlignment;
  uniformBufferStride = (((uniformBufferSize - 1) / minAlignment) + 1) * minAlignment;
  if (!uniformBuffers.InitializeAndMapUniformBuffer(viewFrameResourceCount * uniformBufferStride, *vulkanState->device)) { return false; }
  uniformBuffers.SetDebugNameIfDebugging("Text2DVulkan uniformBuffers");
  
  frameDescriptors.resize(viewFrameResourceCount);
  for (int i = 0; i < frameDescriptors.size(); ++ i) {
    auto& descriptorSet = frameDescriptors[i];
    if (!descriptorSet.Initialize(vulkanShader->perFrameDescriptorSetLayout, descriptorPool, *vulkanState->device)) { return false; }
    descriptorSet.SetDebugNameIfDebugging("Text2DVulkan frameDescriptor");
    
    const VkDescriptorBufferInfo desc = {uniformBuffers, /*offset*/ i * uniformBufferStride, /*range*/ uniformBufferSize};
    descriptorSet.UpdatePerFrameUBO(&desc);
  }
  
  return true;
}

void Text2DVulkan::Destroy() {
  frameDescriptors.clear();
  descriptorPool.Destroy();
  uniformBuffers.Destroy();
}

void Text2DVulkan::GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) {
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  float bounds[4];
  fonsTextBounds(context, x, y, text, nullptr, bounds);
  
  *xMin = bounds[0];
  *yMin = bounds[1];
  *xMax = bounds[2];
  *yMax = bounds[3];
}

void Text2DVulkan::SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  fonsText.vertices.clear();
  fonsText.batches.clear();
  
  int textLen = strlen(text);
  
  if (textLen == 0) { return; }
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  u32 red = 255.f * color.x() + 0.5f;
  u32 green = 255.f * color.y() + 0.5f;
  u32 blue = 255.f * color.z() + 0.5f;
  u32 alpha = 255.f * color.w() + 0.5f;
  fonsSetColor(context, red | (green << 8) | (blue << 16) | (alpha << 24));
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  // fonsSetSpacing(context, float spacing);
  // fonsSetBlur(context, float blur);
  
  /*float newX =*/ fonsCreateText(context, x, y, text, text + textLen, &fonsText);
}

void Text2DVulkan::SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  fonsText.vertices.clear();
  fonsText.batches.clear();
  
  int textLen = strlen(text);
  
  if (textLen == 0) { return; }
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  u32 red = 255.f * color.x() + 0.5f;
  u32 green = 255.f * color.y() + 0.5f;
  u32 blue = 255.f * color.z() + 0.5f;
  u32 alpha = 255.f * color.w() + 0.5f;
  fonsSetColor(context, red | (green << 8) | (blue << 16) | (alpha << 24));
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  // fonsSetSpacing(context, float spacing);
  // fonsSetBlur(context, float blur);
  
  /*float newX =*/ fonsCreateTextBox(context, minX, minY, maxX, maxY, /*wordWrap*/ true, text, text + textLen, &fonsText);
}

void Text2DVulkan::RenderView(int viewIndex, RenderState* renderState) {
  thisFramesRenderState = renderState->AsVulkanRenderState();
  
  // OLD:
  // fonsBatchText(fontStash->GetContext(), fonsText);
  // fonsFlushRender(fontStash->GetContext());
  
  // Loop over all batches in the given text
  int batchEndVertex = fonsText.vertices.size();
  
  for (int b = static_cast<int>(fonsText.batches.size()) - 1; b >= 0; -- b) {
    const auto& textBatch = fonsText.batches[b];
    
    // Render vertices [textBatch.firstVertex, batchEndVertex[
    RenderBatch(fonsText.vertices.data() + textBatch.firstVertex, batchEndVertex - textBatch.firstVertex, textBatch.textureId, viewIndex);
    
    batchEndVertex = textBatch.firstVertex;
  }
}

void Text2DVulkan::SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) {
  if (thisFramesRenderState == nullptr) { return; }
  const int descriptorIndex = viewIndex + thisFramesRenderState->frameInFlightIndex * viewCount;
  memcpy(uniformBuffers.data<u8>() + descriptorIndex * uniformBufferStride, columnMajorModelViewProjectionData, uniformBufferSize);
}

void Text2DVulkan::RenderBatch(FONSVertex* vertices, int count, int textureIndex, int viewIndex) {
  FontStashVulkan* fontStashVulkan = reinterpret_cast<FontStashVulkan*>(fontStash);
  FontStashShaderVulkan* vulkanShader = fontStash->Shader()->GetVulkanImpl();
  const auto& api = thisFramesRenderState->device->Api();
  
  fontstash_vulkan_shader::DescriptorSet0* textureDescriptor = fontStashVulkan->FindDescriptorSetForRendering(textureIndex);
  if (textureDescriptor == nullptr) { return; }
  
  VkBuffer verticesBuffer;
  VkDeviceSize verticesOffset;
  if (!fontStashVulkan->BufferVertices(vertices, count, &verticesBuffer, &verticesOffset)) { return; }
  
  const int descriptorIndex = viewIndex + thisFramesRenderState->frameInFlightIndex * viewCount;
  
  api.vkCmdBindPipeline(*thisFramesRenderState->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanShader->pipeline);
  
  const VkBuffer buffers[1] = {verticesBuffer};
  const VkDeviceSize offsets[1] = {verticesOffset};
  api.vkCmdBindVertexBuffers(*thisFramesRenderState->cmdBuf, /*firstBinding*/ 0, /*bindingCount*/ 1, buffers, offsets);
  const array<VkDescriptorSet, 2> descriptorSets = {
      textureDescriptor->descriptor_set(),
      frameDescriptors[descriptorIndex].descriptor_set()};
  api.vkCmdBindDescriptorSets(
      *thisFramesRenderState->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanShader->pipelineLayout,
      /*firstSet*/ 0, descriptorSets.size(), descriptorSets.data(), /*dynamicOffsetCount*/ 0, /*pDynamicOffsets*/ nullptr);
  
  api.vkCmdDraw(*thisFramesRenderState->cmdBuf, /*vertexCount*/ count, /*instanceCount*/ 1, /*firstVertex*/ 0, /*firstInstance*/ 0);
}

}

#endif
