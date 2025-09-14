#ifdef HAVE_VULKAN

#include "scan_studio/viewer_common/gfx/fontstash_vulkan.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

#include <loguru.hpp>

#include <libvis/io/input_stream.h>

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"

#include <libvis/vulkan/barrier.h>
#include <libvis/vulkan/queue.h>

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

namespace scan_studio {

FontStashVulkan::~FontStashVulkan() {
  Destroy();
}

bool FontStashVulkan::Initialize(int textureWidth, int textureHeight, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  FontStashShaderVulkan* vulkanShader = shader->GetVulkanImpl();
  
  device = vulkanState->device;
  thisFramesRenderState = vulkanState;
  
  // Initialize transfer pool
  if (!transferPool.Initialize(
      device->transfer_queue_family_index(),
      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      *device)) {
    LOG(ERROR) << "transferPool.Initialize() failed"; Destroy(); return false;
  }
  transferPool.SetDebugNameIfDebugging("FontStashVulkan transferPool");
  
  // Initialize descriptor pool
  constexpr int kDescriptorCount = 128;
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorCount);
  if (!descriptorPool.Initialize(/*max_sets*/ kDescriptorCount, *device)) {
    LOG(ERROR) << "Failed to initialize a descriptor pool"; Destroy(); return false;
  }
  descriptorPool.SetDebugNameIfDebugging("FontStashVulkan descriptorPool");
  
  // Initialize texture descriptors in advance
  textureDescriptors.resize(kDescriptorCount);
  for (auto i = 0; i < kDescriptorCount; ++ i) {
    if (!textureDescriptors[i].descriptorSet.Initialize(vulkanShader->perTextureDescriptorSetLayout, descriptorPool, *device)) { LOG(ERROR) << "Failed to initialize a descriptor set"; Destroy(); return false; }
    textureDescriptors[i].fonsTextureIndex = -1;
    textureDescriptors[i].lastUsedInFrameIndex = numeric_limits<int>::min();
  }
  
  // Initialize FontStash (this already calls the create callback, so the Vulkan objects must be initialized at this point)
	FONSparams params;
  
	memset(&params, 0, sizeof(params));
	params.width = textureWidth;
	params.height = textureHeight;
	params.flags = FONS_ZERO_TOPLEFT;
	params.renderCreate = &FontStashVulkan::RenderCreate;
	params.renderResize = &FontStashVulkan::RenderResize;
	params.renderUpdate = &FontStashVulkan::RenderUpdate;
	params.renderDelete = &FontStashVulkan::RenderDelete;
	params.userPtr = this;
  
	context = fonsCreateInternal(&params);
	if (context == nullptr) { return false; }
	
	fonsSetErrorCallback(context, &FontStashVulkan::ErrorCallback, this);
	
	return true;
}

void FontStashVulkan::Destroy() {
  if (context) {
    fonsDeleteInternal(context);
    context = nullptr;
  }
}

int FontStashVulkan::LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) {
  // TODO: We should share the file loading code with FontStashOpenGL, it is identical
  if (context == nullptr) { LOG(ERROR) << "Initialize() has not been called yet"; return false; }
  
  vector<u8> data;
  if (ttfFileStream == nullptr) { LOG(ERROR) << "Tried to load a null file"; return false; }
  if (!ttfFileStream->ReadAll(&data)) { return false; }
  
  return fonsAddFontMem(context, name, std::move(data));
}

void FontStashVulkan::PrepareFrame(RenderState* renderState) {
  thisFramesRenderState = renderState->AsVulkanRenderState();
  
  ++ frameIndex;
  delayedDeleteQueue.SetFrameIndex(frameIndex);
}

bool FontStashVulkan::BufferVertices(const FONSVertex* vertices, int count, VkBuffer* buffer, VkDeviceSize* offset) {
  int geometryBufferIndex;
  int firstIndex;
  if (!FindOrMakeSpaceForVertices(count, &geometryBufferIndex, &firstIndex)) {
    LOG(ERROR) << "Failed to find or make space for " << count << " vertices";
    return false;
  }
  
  GeometryBuffer& geom = geometryBuffers[geometryBufferIndex];
  memcpy(geom.buffer.data<FONSVertex>() + firstIndex, vertices, count * sizeof(FONSVertex));
  
  *buffer = geom.buffer;
  *offset = firstIndex * sizeof(FONSVertex);
  return true;
}

fontstash_vulkan_shader::DescriptorSet0* FontStashVulkan::FindDescriptorSetForRendering(int textureIndex) {
  DescriptorSet* currentDescriptor = nullptr;
  for (int i = 0, size = textureDescriptors.size(); i < size; ++ i) {
    if (textureDescriptors[i].fonsTextureIndex == textureIndex) {
      currentDescriptor = &textureDescriptors[i];
      break;
    }
  }
  if (!currentDescriptor) {
    LOG(ERROR) << "Could not find descriptor for textureIndex " << textureIndex;
    return nullptr;
  }
  
  currentDescriptor->lastUsedInFrameIndex = frameIndex;
  return &currentDescriptor->descriptorSet;
}

bool FontStashVulkan::FindOrMakeSpaceForVertices(int count, int* geometryBufferIndex, int* firstIndex) {
  for (*geometryBufferIndex = 0; *geometryBufferIndex < geometryBuffers.size(); ++ *geometryBufferIndex) {
    GeometryBuffer& geom = geometryBuffers[*geometryBufferIndex];
    
    // Clear outdated used ranges for this geometry buffer
    for (auto it = geom.usedRanges.begin(); it != geom.usedRanges.end(); ) {
      if (it->usedInFrameIndex + thisFramesRenderState->framesInFlightCount <= frameIndex) {
        it = geom.usedRanges.erase(it);
      } else {
        ++ it;
      }
    }
    
    // Search for a free range within this geometry buffer
    *firstIndex = 0;
    
    for (auto it = geom.usedRanges.begin(); it != geom.usedRanges.end(); ++ it) {
      // Is the range [*firstIndex; it->firstIndex - 1] large enough?
      if (it->firstIndex - *firstIndex >= count) {
        // Place the vertices at the start of the free range
        geom.usedRanges.emplace(it, *firstIndex, *firstIndex + count - 1, frameIndex);
        return true;
      }
      
      *firstIndex = it->lastIndex + 1;
    }
    
    // Is the range [*firstIndex; bufferSize - 1] large enough?
    if (geom.buffer.size() / sizeof(FONSVertex) - *firstIndex >= count) {
      // Place the vertices at the start of the free range
      geom.usedRanges.emplace_back(*firstIndex, *firstIndex + count - 1, frameIndex);
      return true;
    }
  }
  
  // There is no free space in the available geometryBuffers. Allocate a new buffer.
  // Since we always copy new data to these buffers and render each range only once,
  // we use host-visible and coherent memory (without separate staging buffers).
  geometryBuffers.emplace_back();
  GeometryBuffer& geom = geometryBuffers.back();
  
  constexpr int kMinBufferVertexCount = 10 * 1024;
  const int bufferVertexCount = max(kMinBufferVertexCount, count);
  
  if (!geom.buffer.InitializeAndMap(
      bufferVertexCount * sizeof(FONSVertex),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, *device)) { geometryBuffers.pop_back(); return false; }
  
  *geometryBufferIndex = geometryBuffers.size() - 1;
  *firstIndex = 0;
  geom.usedRanges.emplace_back(*firstIndex, *firstIndex + count - 1, frameIndex);
  return true;
}

int FontStashVulkan::RenderCreate(int width, int height, int textureIndex) {
  if (texture != nullptr) {
    delayedDeleteQueue.AddForDeletion(frameIndex + thisFramesRenderState->framesInFlightCount, texture);
  }
  texture.reset(new VulkanTexture());
  
  if (textureStaging != nullptr) {
    delayedDeleteQueue.AddForDeletion(frameIndex + thisFramesRenderState->framesInFlightCount, textureStaging);
  }
  textureStaging.reset(new VulkanBuffer());
  
  if (!texture->InitializeEmpty2DTexture(
      width, height, /*mipLevels*/ 1, VK_FORMAT_R8_UNORM,
      *device, &transferPool, device->GetQueue(device->transfer_queue_family_index(), /*queue_index*/ 0),
      VK_FILTER_LINEAR, VK_FILTER_LINEAR,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT)) {
    LOG(ERROR) << "Failed to allocate texture for FontStash";
    return 0;
  }
  
  if (!textureStaging->InitializeAndMapHostTransferSrc(
      width * height * sizeof(u8),
      *device)) {
    LOG(ERROR) << "Failed to allocate texture staging buffer for FontStash";
    return 0;
  }
  
  this->width = width;
  this->height = height;
  
  // Find a descriptor for the new texture and update it
  DescriptorSet* currentDescriptor = nullptr;
  
  for (int i = 0, size = textureDescriptors.size(); i < size; ++ i) {
    if (textureDescriptors[i].lastUsedInFrameIndex + thisFramesRenderState->framesInFlightCount <= frameIndex) {
      currentDescriptor = &textureDescriptors[i];
      
      currentDescriptor->descriptorSet.UpdateFontTexture(&texture->descriptor());
      currentDescriptor->fonsTextureIndex = textureIndex;
      currentDescriptor->lastUsedInFrameIndex = frameIndex;
      break;
    }
  }
  
  if (currentDescriptor == nullptr) {
    LOG(ERROR) << "Did not find any available descriptor. Consider increasing the descriptor pool size or implementing dynamic descriptor pool growing.";
  }
  
  return 1;
}

int FontStashVulkan::RenderResize(int width, int height, int textureIndex) {
  return RenderCreate(width, height, textureIndex);
}

void FontStashVulkan::RenderUpdate(int* rect, const unsigned char* data) {
  if (!texture) { return; }
  
  const int updateWidth = rect[2] - rect[0];
  const int updateHeight = rect[3] - rect[1];
  
  // Copy the data to staging memory
  // TODO: We should modify fontstash such that it directly renders its characters to staging memory to avoid this row-wise copy here
  //       (assuming it only writes to this memory; otherwise, it would be slow, since we use write-combined staging memory).
  for (int row = rect[1]; row < rect[3]; ++ row) {
    const u32 rowOffset = rect[0] + row * width;
    memcpy(textureStaging->data<u8>() + rowOffset, data + rowOffset, updateWidth * sizeof(u8));
  }
  
  // Record the command to copy the staging memory to device memory
  VkBufferImageCopy region = {};
  
  region.bufferOffset = rect[0] + rect[1] * width;
  region.bufferRowLength = width;
  region.bufferImageHeight = height;
  
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  // region.imageSubresource.mipLevel = 0;
  // region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  
  region.imageOffset = {rect[0], rect[1], 0};
  region.imageExtent = {static_cast<u32>(updateWidth), static_cast<u32>(updateHeight), 1};
  
  device->Api().vkCmdCopyBufferToImage(*thisFramesRenderState->cmdBuf, *textureStaging, texture->image(), VK_IMAGE_LAYOUT_GENERAL, 1, &region);
  
  // Record a suitable memory barrier
  VulkanMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT)
      .Submit(*thisFramesRenderState->cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void FontStashVulkan::RenderDelete() {
  texture.reset();
  textureStaging.reset();
  
  textureDescriptors.clear();
  descriptorPool.Destroy();
  
  geometryBuffers.clear();
  
  delayedDeleteQueue.DeleteAll();
}

void FontStashVulkan::ErrorCallback(int error, int val) {
  if (error == FONS_ATLAS_FULL) {
    fonsResetAtlas(context, width, height);  // calls RenderResize(), thus will allocate a new texture
  } else {
    LOG(ERROR) << "Unhandled fontstash error: " << error << ", val: " << val;
  }
}

int FontStashVulkan::RenderCreate(void* userPtr, int width, int height, int textureIndex) {
  FontStashVulkan* fontStash = reinterpret_cast<FontStashVulkan*>(userPtr);
  return fontStash->RenderCreate(width, height, textureIndex);
}

int FontStashVulkan::RenderResize(void* userPtr, int width, int height, int textureIndex) {
  FontStashVulkan* fontStash = reinterpret_cast<FontStashVulkan*>(userPtr);
  return fontStash->RenderResize(width, height, textureIndex);
}

void FontStashVulkan::RenderUpdate(void* userPtr, int* rect, const unsigned char* data) {
  FontStashVulkan* fontStash = reinterpret_cast<FontStashVulkan*>(userPtr);
  fontStash->RenderUpdate(rect, data);
}

void FontStashVulkan::RenderDelete(void* userPtr) {
  FontStashVulkan* fontStash = reinterpret_cast<FontStashVulkan*>(userPtr);
  fontStash->RenderDelete();
}

void FontStashVulkan::ErrorCallback(void* userPtr, int error, int val) {
  FontStashVulkan* fontStash = reinterpret_cast<FontStashVulkan*>(userPtr);
  fontStash->ErrorCallback(error, val);
}

}

#endif
