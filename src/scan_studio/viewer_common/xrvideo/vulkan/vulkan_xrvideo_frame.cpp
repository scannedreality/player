#ifdef HAVE_VULKAN
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_frame.hpp"

#include <array>

#include <loguru.hpp>

#include <libvis/vulkan/barrier.h>
#include <libvis/vulkan/queue.h>
#include <libvis/vulkan/util.h>

#include "scan_studio/common/vulkan_util.hpp"

#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_common_resources.hpp"

namespace scan_studio {

void VulkanXRVideoFrame::Configure(
    VulkanDevice* device) {
  this->device = device;
}

bool VulkanXRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  TimePoint frameLoadingStartTime;
  if (verboseDecoding) {
    frameLoadingStartTime = Clock::now();
  }
  
  const auto& api = device->Api();
  
  this->metadata = metadata;
  
  // Wait for in-flight commands (in case initialization got aborted).
  if (transferFence.is_initialized() && !transferFence.Wait()) {
    LOG(ERROR) << "An error occurred in waiting for transferFence";
  }

  // In case this frame gets reused, destroy the buffers
  vertexBuffer.Destroy();
  indexBuffer.Destroy();
  storageBuffer.Destroy();
  
  // Initialize staging buffers
  // TODO: Staging buffers should be initialized with maximum sizes, managed externally, and given to this class when needed to prevent allocations at runtime
  if (metadata.isKeyframe) {
    if (!verticesStagingBuffer.InitializeAndMapHostTransferSrc(metadata.GetRenderableVertexDataSize(), *device)) { LOG(ERROR) << "Failed to initialize verticesStagingBuffer"; Destroy(); return false; }
    verticesStagingBuffer.SetDebugNameIfDebugging("verticesStagingBuffer");
    
    if (!indicesStagingBuffer.InitializeAndMapHostTransferSrc(metadata.GetIndexDataSize(), *device)) { LOG(ERROR) << "Failed to initialize indicesStagingBuffer"; Destroy(); return false; }
    indicesStagingBuffer.SetDebugNameIfDebugging("indicesStagingBuffer");
  }
  
  const u32 storageBufferSize = metadata.GetDeformationStateDataSize();
  if (!storageStagingBuffer.InitializeAndMapHostTransferSrc(storageBufferSize, *device)) { LOG(ERROR) << "Failed to initialize storageStagingBuffer"; Destroy(); return false; }
  storageStagingBuffer.SetDebugNameIfDebugging("storageStagingBuffer");
  
  if (!textureStagingBuffer.InitializeAndMapHostTransferSrc(metadata.zstdRGBTexture ? (metadata.textureWidth * metadata.textureHeight * 4) : metadata.GetTextureDataSize(), *device)) {
    LOG(ERROR) << "Failed to initialize textureStagingBuffer"; Destroy(); return false;
  }
  textureStagingBuffer.SetDebugNameIfDebugging("textureStagingBuffer");
  
  // Decompress the frame data
  // TODO: Is our access pattern to the write-combined memory buffers okay, or do we have to use page-locked memory only, or better a separate memcpy into write-combined memory?
  // TODO: Once the XRV format contains the vertex alpha size in the metadata (or we get some kind of callback from XRVideoDecompressContent() once it knows the size),
  //       directly decode the vertex alpha into a write-combined memory buffer (unless the access pattern of zstd decoding into it is bad, see the other TODO above ...)
  vector<u8> vertexAlpha;
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      metadata.isKeyframe ? verticesStagingBuffer.data<void>() : nullptr,
      metadata.isKeyframe ? indicesStagingBuffer.data<u16>() : nullptr,
      storageStagingBuffer.data<float>(),
      /*outDuplicatedVertexSourceIndices*/ nullptr,
      &vertexAlpha,
      verboseDecoding)) {
    LOG(ERROR) << "Failed to decompress XRVideo content";
    return false;
  }
  
  if (!vertexAlpha.empty()) {
    if (!alphaStagingBuffer.InitializeAndMapHostTransferSrc(vertexAlpha.size(), *device)) { LOG(ERROR) << "Failed to initialize alphaStagingBuffer"; Destroy(); return false; }
    alphaStagingBuffer.SetDebugNameIfDebugging("alphaStagingBuffer");
    
    memcpy(alphaStagingBuffer.data<u8>(), vertexAlpha.data(), vertexAlpha.size());
  }
  
  VulkanQueue* transferQueue = device->GetQueue(device->transfer_queue_family_index(), /*queue_index*/ 0);
  
  // Initialize vertex and index buffers
  if (metadata.isKeyframe) {
    if (!vertexBuffer.InitializeDeviceLocal(
        metadata.GetRenderableVertexDataSize(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        *device)) { LOG(ERROR) << "Failed to initialize vertexBuffer"; Destroy(); return false; }
    vertexBuffer.SetDebugNameIfDebugging("vertexBuffer");
    
    if (!indexBuffer.InitializeDeviceLocal(
        metadata.GetIndexDataSize(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        *device)) { LOG(ERROR) << "Failed to initialize indexBuffer"; Destroy(); return false; }
    indexBuffer.SetDebugNameIfDebugging("indexBuffer");
  }
  
  // Initialize storage buffer
  if (!storageBuffer.InitializeDeviceLocal(
      storageBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      *device)) { LOG(ERROR) << "Failed to initialize storageBuffer"; Destroy(); return false; }
  storageBuffer.SetDebugNameIfDebugging("storageBuffer");
  
  deformationStateBufferDesc = {};
  deformationStateBufferDesc.buffer = storageBuffer.descriptor().buffer;
  deformationStateBufferDesc.offset = 0;
  deformationStateBufferDesc.range = metadata.GetDeformationStateDataSize();
  
  // Initialize vertex alpha buffer
  if (!vertexAlpha.empty()) {
    if (!vertexAlphaBuffer.InitializeDeviceLocal(
        vertexAlpha.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        *device)) { LOG(ERROR) << "Failed to initialize vertexAlphaBuffer"; Destroy(); return false; }
    vertexAlphaBuffer.SetDebugNameIfDebugging("vertexAlphaBuffer");
  }
  
  // Initialize textures (if not reusing this frame)
  if (metadata.zstdRGBTexture) {
    if (!textureRGB.is_initialized()) {
      if (!textureRGB.InitializeEmpty2DTexture(
          metadata.textureWidth, metadata.textureHeight, /*mipLevels*/ 1, VK_FORMAT_R8G8B8A8_SRGB,
          *device, /*transferPool*/ nullptr, /*transferQueue*/ VK_NULL_HANDLE,
          VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED)) { LOG(ERROR) << "Failed to initialize textureRGB"; Destroy(); return false; }
      textureRGB.image().SetDebugNameIfDebugging("textureRGB.image");
      textureRGB.view().SetDebugNameIfDebugging("textureRGB.view");
    }
  } else {
    if (!textureLuma.is_initialized()) {
      if (!textureLuma.InitializeEmpty2DTexture(
          metadata.textureWidth, metadata.textureHeight, /*mipLevels*/ 1, VK_FORMAT_R8_UNORM,
          *device, /*transferPool*/ nullptr, /*transferQueue*/ VK_NULL_HANDLE,
          VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED)) { LOG(ERROR) << "Failed to initialize textureLuma"; Destroy(); return false; }
      textureLuma.image().SetDebugNameIfDebugging("textureLuma.image");
      textureLuma.view().SetDebugNameIfDebugging("textureLuma.view");
    }
    
    for (auto& texture : {&textureChromaU, &textureChromaV}) {
      if (!texture->is_initialized()) {
        if (!texture->InitializeEmpty2DTexture(
            metadata.textureWidth / 2, metadata.textureHeight / 2, /*mipLevels*/ 1, VK_FORMAT_R8_UNORM,
            *device, /*transferPool*/ nullptr, /*transferQueue*/ VK_NULL_HANDLE,
            VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_LAYOUT_UNDEFINED)) { LOG(ERROR) << "Failed to initialize textureChromaU or textureChromaV"; Destroy(); return false; }
        texture->image().SetDebugNameIfDebugging((texture == &textureChromaU) ? "textureChromaU.image" : "textureChromaV.image");
        texture->view().SetDebugNameIfDebugging((texture == &textureChromaU) ? "textureChromaU.view" : "textureChromaV.view");
      }
    }
  }
  
  // Get the texture data
  TimePoint textureWaitStartTime; if (verboseDecoding) { textureWaitStartTime = Clock::now(); }
  if (!textureFramePromise->Wait()) {
    Destroy(); return false;
  }
  TimePoint textureWaitEndTime; if (verboseDecoding) { textureWaitEndTime = Clock::now(); }
  
  if (metadata.zstdRGBTexture) {
    if (metadata.compressedRGBSize > 0) {
      vector<u8> rgbData;
      textureFramePromise->TakeRGB(&rgbData);
      
      // Convert RGB to RGBX.
      // Note: If this was performance-sensitive, then it would make sense to do this data unpacking on the GPU.
      const u8* src = rgbData.data();
      const u8* srcEnd = src + metadata.textureWidth * metadata.textureHeight * 3;
      u8* dest = textureStagingBuffer.data<u8>();
      while (src < srcEnd) {
        *dest = *src;
        ++ src; ++ dest;
        *dest = *src;
        ++ src; ++ dest;
        *dest = *src;
        ++ src; ++ dest;
        *dest = 255;
        ++ dest;
      }
    } else {
      memset(textureStagingBuffer.data<u8>(), 0, metadata.textureWidth * metadata.textureHeight * 4);
    }
  } else {
    auto textureData = textureFramePromise->Take();
    // TODO: Try to use zero-copy to improve performance, handling shared-CPU-GPU-memory devices and devices with dedicated GPU memory
    if (textureData) {
      XRVideoCopyTexture(*textureData, textureStagingBuffer.data<u8>(), verboseDecoding);
    } else {
      memset(textureStagingBuffer.data<u8>(), 0, metadata.GetTextureDataSize());
    }
    textureData.reset();
  }
  
  // If we have a left-over transferCmdBuf (from a previous frame where initialization was aborted),
  // release it before reallocating the transferPool that it was created from.
  transferCmdBuf.reset();

  // Upload the data in the staging buffers to the GPU.
  //
  // Note that we allocate a separate command pool for each frame that gets transferred
  // (because host access to command pools must be synchronized even for just recording commands into command buffers allocated from them,
  // or deallocating command buffers - this happens in WaitForResourceTransfers() that may be called by a different thread than Initialize(),
  // and we'd rather not put mutexes everywhere the command buffer is used in Initialize()).
  //
  // In case this is a performance concern, we could maintain a cached set of command pools per XRVideo, and take a pool from this
  // set if one is available and reset it instead of allocating a new one. (However, timing showed that at least on my Linux desktop,
  // allocating a new command pool usually only takes like up to 0.005 ms, with the average frame decoding time currently being somewhere
  // around 5 ms (1000x of the command pool allocation time), so it is probably of low priority.)
  if (!transferPool.Initialize(device->transfer_queue_family_index(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, *device)) {
    LOG(ERROR) << "Failed to initialize transferPool"; Destroy(); return false;
  }
  
  transferCmdBuf = transferPool.BeginOneTimeCommands();
  if (!transferCmdBuf) { LOG(ERROR) << "Failed to initialize transferCmdBuf"; Destroy(); return false; }

  if (metadata.isKeyframe) {
    verticesStagingBuffer.CmdCopyBuffer(*transferCmdBuf, &vertexBuffer, /*srcOffset*/ 0, /*dstOffset*/ 0, /*size*/ vertexBuffer.size());
    indicesStagingBuffer.CmdCopyBuffer(*transferCmdBuf, &indexBuffer, /*srcOffset*/ 0, /*dstOffset*/ 0, /*size*/ indexBuffer.size());
  }
  
  storageStagingBuffer.CmdCopyBuffer(*transferCmdBuf, &storageBuffer, /*srcOffset*/ 0, /*dstOffset*/ 0, /*size*/ storageBufferSize);
  
  if (!vertexAlpha.empty()) {
    alphaStagingBuffer.CmdCopyBuffer(*transferCmdBuf, &vertexAlphaBuffer, /*srcOffset*/ 0, /*dstOffset*/ 0, /*size*/ vertexAlpha.size());
  }
  
  for (auto& texture : metadata.zstdRGBTexture ? vector<VulkanTexture*>{&textureRGB} : vector<VulkanTexture*>{&textureLuma, &textureChromaU, &textureChromaV}) {
    texture->image().CmdTransitionLayout(
        *transferCmdBuf,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  // do not wait for previous commands
        VK_PIPELINE_STAGE_TRANSFER_BIT);  // make following commands wait from TRANSFER stage on
  }
  
  if (metadata.zstdRGBTexture) {
    textureRGB.image().CmdCopyFromBuffer(*transferCmdBuf, textureStagingBuffer);
  } else {
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(VkBufferImageCopy));
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {metadata.textureWidth, metadata.textureHeight, 1};
    api.vkCmdCopyBufferToImage(*transferCmdBuf, textureStagingBuffer, textureLuma.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    region.bufferOffset = metadata.textureWidth * metadata.textureHeight;
    region.imageExtent = {metadata.textureWidth / 2, metadata.textureHeight / 2, 1};
    api.vkCmdCopyBufferToImage(*transferCmdBuf, textureStagingBuffer, textureChromaU.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    region.bufferOffset = (metadata.textureWidth * metadata.textureHeight * 5) / 4;
    api.vkCmdCopyBufferToImage(*transferCmdBuf, textureStagingBuffer, textureChromaV.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }
  
  vector<VulkanBufferMemoryBarrier> bufferBarriers;
  if (storageBufferSize > 0) {
    bufferBarriers.push_back(VulkanBufferMemoryBarrier(storageBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, 0, device->transfer_queue_family_index(), device->graphics_queue_family_index(), storageBufferSize));
  }
  if (!vertexAlpha.empty()) {
    bufferBarriers.emplace_back(vertexAlphaBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, 0, device->transfer_queue_family_index(), device->graphics_queue_family_index(), vertexAlpha.size());
  }
  if (metadata.isKeyframe && indexBuffer.size() > 0) {
    bufferBarriers.emplace_back(vertexBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, 0, device->transfer_queue_family_index(), device->graphics_queue_family_index(), vertexBuffer.size());
    bufferBarriers.emplace_back(indexBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, 0, device->transfer_queue_family_index(), device->graphics_queue_family_index(), indexBuffer.size());
  }
  if (!bufferBarriers.empty()) {
    VulkanCmdPipelineBarrier(
        *transferCmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,  // wait for TRANSFER stage of previous commands to complete
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        bufferBarriers);
  }
  
  for (auto& texture : metadata.zstdRGBTexture ? vector<VulkanTexture*>{&textureRGB} : vector<VulkanTexture*>{&textureLuma, &textureChromaU, &textureChromaV}) {
    // NOTE: The commented code here implements combined layout transition and ownership transfer.
    //       I believe that this should be correct in principle, however, the validation layers on the Quest 2
    //       complain about submitted command buffers assuming wrong image layouts then.
    //       I think that this happens because the layers are confused about the "release" barrier doing the
    //       layout transition already, and then say that there is an error when the "aquire" barrier
    //       repeats the transition info (and seemingly assumes the wrong initial layout for that transition).
    //
    //       Discussions on the Internet indicate that doing that should be fine. However, to be on the
    //       safe side (given that GPU drivers might get that wrong too),
    //       I split up the layout transition and the queue ownership transfer below.
    //
    // texture->image().CmdTransitionLayoutRelease(
    //     *transferCmdBuf,
    //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //     VK_ACCESS_TRANSFER_WRITE_BIT,
    //     VK_PIPELINE_STAGE_TRANSFER_BIT,
    //     device->graphics_queue_family_index());
    
    // Separate layout transition and ownership transfer:
    texture->image().CmdTransitionLayout(
        *transferCmdBuf,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    texture->image().CmdTransitionLayoutRelease(
        *transferCmdBuf,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        device->graphics_queue_family_index());
  }
  
  if (!transferFence.is_initialized()) {
    if (!transferFence.Initialize(/*create_signaled*/ false, *device)) {
      LOG(ERROR) << "Failed to initialize transferFence"; Destroy(); return false;
    }
  } else {
    transferFence.Reset();
  }
  
  // Submit the operations.
  if (!transferCmdBuf->Submit(/*wait_for_completion*/ false, transferQueue, transferFence)) {
    LOG(ERROR) << "Failed to submit transferCmdBuf";
    Destroy();
    return false;
  }
  
  ownedByGraphicsQueueFamily = false;
  
  if (verboseDecoding) {
    const TimePoint frameLoadingEndTime = Clock::now();
    const float loadingTime =
        MillisecondsDuration(textureWaitStartTime - frameLoadingStartTime).count() +
        MillisecondsDuration(frameLoadingEndTime - textureWaitEndTime).count();
    LOG(1) << "VulkanXRVideoFrame: Frame loading time without I/O and texture decoding / wait: " << loadingTime << " ms";
  }
  return true;
}

void VulkanXRVideoFrame::Destroy() {
  // Wait for in-flight commands (in case initialization got aborted).
  if (transferFence.is_initialized() && !transferFence.Wait()) {
    LOG(ERROR) << "An error occurred in waiting for transferFence";
  }

  transferCmdBuf.reset();
  transferPool.Destroy();
  
  verticesStagingBuffer.Destroy();
  indicesStagingBuffer.Destroy();
  
  storageStagingBuffer.Destroy();
  alphaStagingBuffer.Destroy();
  textureStagingBuffer.Destroy();
  
  vertexBuffer.Destroy();
  indexBuffer.Destroy();
  storageBuffer.Destroy();
  vertexAlphaBuffer.Destroy();
  
  textureLuma.Destroy();
  textureChromaU.Destroy();
  textureChromaV.Destroy();
  
  textureRGB.Destroy();
}

void VulkanXRVideoFrame::WaitForResourceTransfers() {
  if (!transferFence.Wait()) {
    LOG(ERROR) << "An error occurred in waiting for transferFence";
  }
  
  transferCmdBuf.reset();
  transferPool.Destroy();
  
  verticesStagingBuffer.Destroy();
  indicesStagingBuffer.Destroy();
  
  storageStagingBuffer.Destroy();
  alphaStagingBuffer.Destroy();
  textureStagingBuffer.Destroy();
}

void VulkanXRVideoFrame::EnsureResourcesAreInGraphicsQueueFamily(VulkanCommandBuffer* cmdBuf) {
  // If this is the first time we display this frame after its upload,
  // then we have to insert memory barriers to finish its transfer
  // from the transfer queue to the graphics queue. These barriers must be
  // the same as those submitted in the transfer queue to release ownership.
  //
  // This barrier must be submitted before the start of the render pass
  // (unless that has some specific dependency on itself).
  if (!ownedByGraphicsQueueFamily) {
    vector<VulkanBufferMemoryBarrier> bufferBarriers;
    if (storageBuffer.size() > 0) {
      bufferBarriers.push_back(VulkanBufferMemoryBarrier(storageBuffer, 0, VK_ACCESS_SHADER_READ_BIT, device->transfer_queue_family_index(), device->graphics_queue_family_index(), storageBuffer.size()));
    }
    if (vertexAlphaBuffer.size() > 0) {
      bufferBarriers.emplace_back(VulkanBufferMemoryBarrier(vertexAlphaBuffer, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, device->transfer_queue_family_index(), device->graphics_queue_family_index(), vertexAlphaBuffer.size()));
    }
    if (metadata.isKeyframe && indexBuffer.size() > 0) {
      bufferBarriers.emplace_back(VulkanBufferMemoryBarrier(vertexBuffer, 0, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, device->transfer_queue_family_index(), device->graphics_queue_family_index(), vertexBuffer.size()));
      bufferBarriers.emplace_back(VulkanBufferMemoryBarrier(indexBuffer, 0, VK_ACCESS_INDEX_READ_BIT, device->transfer_queue_family_index(), device->graphics_queue_family_index(), indexBuffer.size()));
    }
    if (!bufferBarriers.empty()) {
      VulkanCmdPipelineBarrier(
          *cmdBuf,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          bufferBarriers);
    }
    
    for (auto& texture : metadata.zstdRGBTexture ? vector<VulkanTexture*>{&textureRGB} : vector<VulkanTexture*>{&textureLuma, &textureChromaU, &textureChromaV}) {
      // See the detailed comment on the matching "release" barrier for why
      // we use the variant below instead of the commented-out variant here.
      //
      // texture->image().CmdTransitionLayoutAcquire(
      //     *cmdBuf,
      //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      //     VK_ACCESS_SHADER_READ_BIT,
      //     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      //     device->transfer_queue_family_index());
      
      texture->image().CmdTransitionLayoutAcquire(
          *cmdBuf,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_ACCESS_SHADER_READ_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          device->transfer_queue_family_index());
    }
    
    ownedByGraphicsQueueFamily = true;
  }
}

void VulkanXRVideoFrame::PrepareRender(
    VulkanCommandBuffer* cmdBuf,
    vulkan_xrvideo_shader::DescriptorSet0* descriptorSet,
    const VkDescriptorBufferInfo& interpolatedDeformationStateBufferInfo,
    VulkanXRVideoFrame* lastKeyframe) {
  VulkanXRVideoFrame* baseFrame = metadata.isKeyframe ? this : lastKeyframe;
  if (baseFrame->metadata.indexCount == 0) {
    return;
  }
  
  EnsureResourcesAreInGraphicsQueueFamily(cmdBuf);
  baseFrame->EnsureResourcesAreInGraphicsQueueFamily(cmdBuf);
  
  // Use the current texture
  if (metadata.zstdRGBTexture) {
    textureRGB.UpdateLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    descriptorSet->UpdateTextureLuma(&textureRGB.descriptor());
    descriptorSet->UpdateTextureChromaU(&textureRGB.descriptor());
    descriptorSet->UpdateTextureChromaV(&textureRGB.descriptor());
  } else {
    textureLuma.UpdateLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    textureChromaU.UpdateLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    textureChromaV.UpdateLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    descriptorSet->UpdateTextureLuma(&textureLuma.descriptor());
    descriptorSet->UpdateTextureChromaU(&textureChromaU.descriptor());
    descriptorSet->UpdateTextureChromaV(&textureChromaV.descriptor());
  }
  
  // Use the interpolated deformation state
  descriptorSet->UpdateDeformationStateStorageBuffer(&interpolatedDeformationStateBufferInfo);
}

void VulkanXRVideoFrame::Render(
    VulkanCommandBuffer* cmdBuf,
    bool useSurfaceNormalShading,
    vulkan_xrvideo_shader::DescriptorSet0* descriptorSet,
    VulkanBuffer* uniformBuffer,
    const VulkanXRVideoFrame* lastKeyframe,
    const VulkanXRVideo* xrVideo) const {
  const VulkanXRVideoFrame* baseFrame = metadata.isKeyframe ? this : lastKeyframe;
  const auto& baseFrameMetadata = baseFrame->metadata;
  if (baseFrameMetadata.indexCount == 0) {
    return;
  }
  
  const bool useVertexAlpha = !useSurfaceNormalShading && vertexAlphaBuffer.size() > 0;
  
  const auto& api = device->Api();
  
  // Update the uniform buffers
  auto setUniformBuffer = [this, &baseFrameMetadata](auto* ubv) {
    ubv->invTextureWidth = 1.f / (metadata.zstdRGBTexture ? textureRGB : textureLuma).image().width();
    ubv->invTextureHeight = 1.f / (metadata.zstdRGBTexture ? textureRGB : textureLuma).image().height();
    ubv->bboxMinX = baseFrameMetadata.bboxMinX;
    ubv->bboxMinY = baseFrameMetadata.bboxMinY;
    ubv->bboxMinZ = baseFrameMetadata.bboxMinZ;
    ubv->vertexFactorX = baseFrameMetadata.vertexFactorX;
    ubv->vertexFactorY = baseFrameMetadata.vertexFactorY;
    ubv->vertexFactorZ = baseFrameMetadata.vertexFactorZ;
  };
  
  const VulkanPipelineLayout* renderPipelineLayout;
  const VulkanPipeline* renderPipeline;
  
  if (useSurfaceNormalShading) {
    VulkanXRVideo::UniformBufferDataVertex_NormalShading* ubv = uniformBuffer->data<VulkanXRVideo::UniformBufferDataVertex_NormalShading>();
    setUniformBuffer(ubv);
    
    renderPipelineLayout = &xrVideo->CommonResources()->normalShadingPipelineLayout;
    renderPipeline = &xrVideo->CommonResources()->normalShadingPipeline;
  } else {
    VulkanXRVideo::UniformBufferDataVertex* ubv = uniformBuffer->data<VulkanXRVideo::UniformBufferDataVertex>();
    setUniformBuffer(ubv);
    
    renderPipelineLayout = &xrVideo->CommonResources()->pipelineLayout;
    
    if (useVertexAlpha) {
      renderPipeline = &xrVideo->CommonResources()->alphaPipeline;
    } else {
      renderPipeline = &xrVideo->CommonResources()->pipeline;
    }
  }
  
  // Render the mesh
  api.vkCmdBindPipeline(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, *renderPipeline);
  
  const VkBuffer vertexBuffers[2] = {baseFrame->vertexBuffer.buffer(), vertexAlphaBuffer.buffer()};
  const VkDeviceSize offsets[2] = {0, 0};
  api.vkCmdBindVertexBuffers(*cmdBuf, 0, useVertexAlpha ? 2 : 1, vertexBuffers, offsets);
  api.vkCmdBindIndexBuffer(*cmdBuf, baseFrame->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
  api.vkCmdBindDescriptorSets(*cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, *renderPipelineLayout, 0, 1, &descriptorSet->descriptor_set(), 0, nullptr);
  
  if (!useSurfaceNormalShading) {
    const float pushConstants[3] = {
        static_cast<float>((metadata.zstdRGBTexture ? textureRGB : textureLuma).image().width()),
        static_cast<float>((metadata.zstdRGBTexture ? textureRGB : textureLuma).image().height()),
        /*useRGBTexture*/ metadata.zstdRGBTexture ? 1.f : 0.f};
    api.vkCmdPushConstants(*cmdBuf, *renderPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 3 * sizeof(float), pushConstants);
  }
  
  api.vkCmdDrawIndexed(*cmdBuf, baseFrameMetadata.indexCount, /*instanceCount*/ 1, /*firstIndex*/ 0, /*vertexOffset*/ 0, /*firstInstance*/ 0);
}

}

#endif
