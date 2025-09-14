#ifdef __APPLE__
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_frame.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_common_resources.hpp"

namespace scan_studio {

void MetalXRVideoFrame::Configure(
    MTL::CommandQueue* transferQueue,
    MTL::Device* device) {
  this->transferQueue = transferQueue;
  this->device = device;
  blitCompleteSync.reset(new BlitCompleteSync());
}

void MetalXRVideoFrame::UseExternalBuffers(MTL::Buffer* vertexBuffer, MTL::Buffer* indexBuffer, MTL::Buffer* storageBuffer, MTL::Buffer* alphaBuffer) {
  this->vertexBuffer = NS::RetainPtr(vertexBuffer);
  this->indexBuffer = NS::RetainPtr(indexBuffer);
  this->storageBuffer = NS::RetainPtr(storageBuffer);
  this->alphaBuffer = NS::RetainPtr(alphaBuffer);
  
  // Debug: Print some information about the externally received resources
  // auto printResourceInfo = [](const char* name, MTL::Resource* resource) {
  //   LOG(1) << name << ": storageMode: " << resource->storageMode() << ", resourceOptions: " << resource->resourceOptions()
  //          << ", hazardTrackingMode: " << resource->hazardTrackingMode() << ", cpuCacheMode: " << resource->cpuCacheMode();
  // };
  // printResourceInfo("Vertex buffer", vertexBuffer);
  // printResourceInfo("Index buffer", indexBuffer);
  // printResourceInfo("Storage buffer", storageBuffer);
  // printResourceInfo("Alpha buffer", alphaBuffer);
  
  useExternalBuffers = true;
}

// void MetalXRVideoFrame::UseExternalTextures(MTL::Texture* textureLuma, MTL::Texture* textureChromaU, MTL::Texture* textureChromaV) {
//   this->textureLuma = NS::RetainPtr(textureLuma);
//   this->textureChromaU = NS::RetainPtr(textureChromaU);
//   this->textureChromaV = NS::RetainPtr(textureChromaV);
//   
//   // Debug: Print some information about the externally received resources
//   // auto printResourceInfo = [](const char* name, MTL::Resource* resource) {
//   //   LOG(1) << name << ": storageMode: " << resource->storageMode() << ", resourceOptions: " << resource->resourceOptions()
//   //          << ", hazardTrackingMode: " << resource->hazardTrackingMode() << ", cpuCacheMode: " << resource->cpuCacheMode();
//   // };
//   // printResourceInfo("Texture (luma)", textureLuma);
//   // printResourceInfo("Texture (chroma U)", textureChromaU);
//   // printResourceInfo("Texture (chroma V)", textureChromaV);
//   
//   useExternalTextures = true;
// }

bool MetalXRVideoFrame::InitializeTextures(u32 textureWidth, u32 textureHeight) {
  auto textureDesc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR8Unorm, textureWidth, textureHeight, /*mipmapped*/ false);
  textureDesc->setStorageMode(MTL::StorageModePrivate);
  textureDesc->setUsage(MTL::TextureUsageShaderRead);
  textureDesc->setCpuCacheMode(MTL::CPUCacheMode::CPUCacheModeWriteCombined);
  
  textureLuma = NS::TransferPtr(device->newTexture(textureDesc));
  if (!textureLuma) { LOG(ERROR) << "Failed to initialize textureLuma"; Destroy(); return false; }
  textureLuma->setLabel(NS::String::string("textureLuma", NS::UTF8StringEncoding));
  
  textureDesc->setWidth(textureWidth / 2);
  textureDesc->setHeight(textureHeight / 2);
  
  textureChromaU = NS::TransferPtr(device->newTexture(textureDesc));
  if (!textureChromaU) { LOG(ERROR) << "Failed to initialize textureChromaU"; Destroy(); return false; }
  textureChromaU->setLabel(NS::String::string("textureChromaU", NS::UTF8StringEncoding));
  
  textureChromaV = NS::TransferPtr(device->newTexture(textureDesc));
  if (!textureChromaV) { LOG(ERROR) << "Failed to initialize textureChromaV"; Destroy(); return false; }
  textureChromaV->setLabel(NS::String::string("textureChromaV", NS::UTF8StringEncoding));
  
  return true;
}

bool MetalXRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  TimePoint frameLoadingStartTime;
  if (verboseDecoding) {
    frameLoadingStartTime = Clock::now();
  }
  
  shared_ptr<NS::AutoreleasePool> initializationAutoreleasePool(NS::AutoreleasePool::alloc()->init(), [](NS::AutoreleasePool* pool) { pool->release(); });
  
  // In case this frame gets reused and owns its resources, destroy the buffers
  if (!useExternalBuffers) {
    vertexBuffer.reset();
    indexBuffer.reset();
    storageBuffer.reset();
    alphaBuffer.reset();
  }
  
  // Store the metadata
  this->metadata = metadata;
  
  // Note on the thread-safety of the resource initializations below:
  //   While the Metal docs don't seem to mention whether newBuffer(), newTexture() are thread-safe,
  //   there is a thread here (https://developer.apple.com/forums/thread/93346) where an Apple
  //   "Graphics and Games Engineer" states in a response to the question
  //   "Can I allocate resources from a MTLDevice safely from multiple threads?":
  //   "You should be able to safely alloc resources from a device."
  
  // Initialize staging buffers (if a non-unified memory model is used).
  // See here for recommendations for which Metal resource storage modes to use:
  // https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html#//apple_ref/doc/uid/TP40016642-CH17-SW1
  
  // TODO: Staging buffers should be initialized with maximum sizes, managed externally, and given to this class when needed to prevent allocations at runtime
  const bool useStagingBuffers =
      (useExternalBuffers && ((vertexBuffer->storageMode() & MTL::ResourceStorageModePrivate) ||
                              (indexBuffer->storageMode() & MTL::ResourceStorageModePrivate) ||
                              (storageBuffer->storageMode() & MTL::ResourceStorageModePrivate) ||
                              (alphaBuffer->storageMode() & MTL::ResourceStorageModePrivate))) ||
      (!useExternalBuffers && !device->hasUnifiedMemory());
  
  if (useStagingBuffers) {
    if (metadata.isKeyframe) {
      verticesStagingBuffer = NS::TransferPtr(device->newBuffer(metadata.GetRenderableVertexDataSize(), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
      if (!verticesStagingBuffer) { LOG(ERROR) << "Failed to allocate verticesStagingBuffer"; Destroy(); return false; }
      verticesStagingBuffer->setLabel(NS::String::string("verticesStagingBuffer", NS::UTF8StringEncoding));
      
      indicesStagingBuffer = NS::TransferPtr(device->newBuffer(metadata.GetIndexDataSize(), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
      if (!indicesStagingBuffer) { LOG(ERROR) << "Failed to allocate indicesStagingBuffer"; Destroy(); return false; }
      indicesStagingBuffer->setLabel(NS::String::string("indicesStagingBuffer", NS::UTF8StringEncoding));
    }
    
    storageStagingBuffer = NS::TransferPtr(device->newBuffer(metadata.GetDeformationStateDataSize(), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
    if (!storageStagingBuffer) { LOG(ERROR) << "Failed to allocate storageStagingBuffer"; Destroy(); return false; }
    storageStagingBuffer->setLabel(NS::String::string("storageStagingBuffer", NS::UTF8StringEncoding));
  }
  
  // Initialize a staging buffer for the textures.
  // In contrast to the other resources, we always use a staging buffer for these,
  // as we can never directly write into texture storage; directly writing into a buffer and then creating the three textures
  // from this buffer would be possible, but Metal synchronizes access to textures allocated from the same buffer, and cannot
  // perform texture-specific optimizations for textures allocated from buffers.
  // So, it seemed like it might be best to always write into a staging buffer first and then blit into the three textures.
  textureStagingBuffer = NS::TransferPtr(device->newBuffer(metadata.GetTextureDataSize(), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
  if (!textureStagingBuffer) { LOG(ERROR) << "Failed to allocate textureStagingBuffer"; Destroy(); return false; }
  textureStagingBuffer->setLabel(NS::String::string("textureStagingBuffer", NS::UTF8StringEncoding));
  
  // Initialize final resources (unless in external-resource mode, where we only check that the existing external buffers are sufficiently large)
  if (useExternalBuffers) {
    if (metadata.isKeyframe) {
      if (metadata.GetRenderableVertexDataSize() > vertexBuffer->length()) {
        LOG(ERROR) << "External vertex buffer is too small. Available bytes: " << vertexBuffer->length() << ". Required bytes: " << metadata.GetRenderableVertexDataSize(); Destroy(); return false;
      }
      if (metadata.GetIndexDataSize() > indexBuffer->length()) {
        LOG(ERROR) << "External index buffer is too small. Available bytes: " << indexBuffer->length() << ". Required bytes: " << metadata.GetIndexDataSize(); Destroy(); return false;
      }
    }
    if (metadata.GetDeformationStateDataSize() > storageBuffer->length()) {
      LOG(ERROR) << "External storage buffer is too small. Available bytes: " << storageBuffer->length() << ". Required bytes: " << metadata.GetDeformationStateDataSize(); Destroy(); return false;
    }
  } else {
    const MTL::ResourceOptions finalResourceOptions = useStagingBuffers ? MTL::ResourceStorageModePrivate : (MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
    
    if (metadata.isKeyframe) {
      vertexBuffer = NS::TransferPtr(device->newBuffer(metadata.GetRenderableVertexDataSize(), finalResourceOptions));
      if (!vertexBuffer) { LOG(ERROR) << "Failed to allocate vertexBuffer"; Destroy(); return false; }
      vertexBuffer->setLabel(NS::String::string("vertexBuffer", NS::UTF8StringEncoding));
      
      indexBuffer = NS::TransferPtr(device->newBuffer(metadata.GetIndexDataSize(), finalResourceOptions));
      if (!indexBuffer) { LOG(ERROR) << "Failed to allocate indexBuffer"; Destroy(); return false; }
      indexBuffer->setLabel(NS::String::string("indexBuffer", NS::UTF8StringEncoding));
    }
    
    storageBuffer = NS::TransferPtr(device->newBuffer(metadata.GetDeformationStateDataSize(), finalResourceOptions));
    if (!storageBuffer) { LOG(ERROR) << "Failed to allocate storageBuffer"; Destroy(); return false; }
    storageBuffer->setLabel(NS::String::string("storageBuffer", NS::UTF8StringEncoding));
  }
  
  // Initialize textures (if not already allocated, e.g., when reusing this frame)
  if (!textureLuma || !textureChromaU || !textureChromaV) {
    if (!InitializeTextures(metadata.textureWidth, metadata.textureHeight)) {
      Destroy(); return false;
    }
  }
  
  // Decompress the frame data
  // TODO: Is our access pattern to the write-combined memory buffers okay, or do we have to use page-locked memory only, or better a separate memcpy into write-combined memory?
  vector<u8> vertexAlpha;
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      metadata.isKeyframe ? (useStagingBuffers ? verticesStagingBuffer->contents() : vertexBuffer->contents()) : nullptr,
      static_cast<u16*>(metadata.isKeyframe ? (useStagingBuffers ? indicesStagingBuffer->contents() : indexBuffer->contents()) : nullptr),
      static_cast<float*>(useStagingBuffers ? storageStagingBuffer->contents() : storageBuffer->contents()),
      /*outDuplicatedVertexSourceIndices*/ nullptr,
      &vertexAlpha,
      verboseDecoding)) {
    LOG(ERROR) << "Failed to decompress XRVideo content";
    Destroy(); return false;
  }
  
  // Allocate, map, and copy to alpha (staging) buffer
  // (since we only know this buffer's size after XRVideoDecompressContent()).
  const u32 vertexAlphaSize = vertexAlpha.size() * sizeof(*vertexAlpha.data());
  
  if (vertexAlphaSize > 0) {
    if (useStagingBuffers) {
      alphaStagingBuffer = NS::TransferPtr(device->newBuffer(vertexAlphaSize, MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
      if (!alphaStagingBuffer) { LOG(ERROR) << "Failed to allocate alphaStagingBuffer"; Destroy(); return false; }
      alphaStagingBuffer->setLabel(NS::String::string("alphaStagingBuffer", NS::UTF8StringEncoding));
    }
    
    if (useExternalBuffers) {
      if (vertexAlphaSize > alphaBuffer->length()) {
        LOG(ERROR) << "External alpha buffer is too small. Available bytes: " << alphaBuffer->length() << ". Required bytes: " << vertexAlphaSize; Destroy(); return false;
      }
    } else {
      LOG(FATAL) << "Only the useExternalBuffers path is implemented currently";
    }
    
    memcpy(useStagingBuffers ? alphaStagingBuffer->contents() : alphaBuffer->contents(), vertexAlpha.data(), vertexAlphaSize);
  }
  
  // Get the texture data
  if (!textureFramePromise->Wait()) {
    Destroy(); return false;
  }
  auto textureData = textureFramePromise->Take();
  // TODO: Try to use zero-copy to improve performance, handling shared-CPU-GPU-memory devices and devices with dedicated GPU memory
  if (textureData) {
    XRVideoCopyTexture(*textureData, static_cast<u8*>(textureStagingBuffer->contents()), verboseDecoding);
  } else {
    memset(static_cast<u8*>(textureStagingBuffer->contents()), 0, (3 * metadata.textureWidth * metadata.textureHeight) / 2);
  }
  textureData.reset();
  
  // Upload the data in the staging buffers to the GPU.
  MTL::CommandBuffer* cmdBuf = transferQueue->commandBuffer();
  if (!cmdBuf) { LOG(ERROR) << "Failed to initialize cmdBuf"; Destroy(); return false; }
  
  MTL::BlitCommandEncoder* blitEncoder = cmdBuf->blitCommandEncoder();
  if (!blitEncoder) { LOG(ERROR) << "Failed to initialize blitEncoder"; Destroy(); return false; }
  
  if (useStagingBuffers) {
    if (metadata.isKeyframe) {
      blitEncoder->copyFromBuffer(verticesStagingBuffer.get(), /*sourceOffset*/ 0, vertexBuffer.get(), /*destinationOffset*/ 0, /*size*/ metadata.GetRenderableVertexDataSize());
      blitEncoder->copyFromBuffer(indicesStagingBuffer.get(), /*sourceOffset*/ 0, indexBuffer.get(), /*destinationOffset*/ 0, /*size*/ metadata.GetIndexDataSize());
    }
    
    blitEncoder->copyFromBuffer(storageStagingBuffer.get(), /*sourceOffset*/ 0, storageBuffer.get(), /*destinationOffset*/ 0, /*size*/ metadata.GetDeformationStateDataSize());
    if (vertexAlphaSize > 0) {
      blitEncoder->copyFromBuffer(alphaStagingBuffer.get(), /*sourceOffset*/ 0, alphaBuffer.get(), /*destinationOffset*/ 0, /*size*/ vertexAlphaSize);
    }
  } else if (useExternalBuffers) {
    // We don't use staging buffers, and we are in external buffers mode.
    // This means that we directly wrote to the contents of vertexBuffer, indexBuffer, and storageBuffer, and they might have 'managed' storage mode.
    // If so, we must tell Metal that we changed them.
    if (metadata.isKeyframe) {
      if (vertexBuffer->storageMode() == MTL::StorageModeManaged) {
        vertexBuffer->didModifyRange(NS::Range(0, metadata.GetRenderableVertexDataSize()));
      }
      if (indexBuffer->storageMode() == MTL::StorageModeManaged) {
        indexBuffer->didModifyRange(NS::Range(0, metadata.GetIndexDataSize()));
      }
    }
    if (storageBuffer->storageMode() == MTL::StorageModeManaged) {
      storageBuffer->didModifyRange(NS::Range(0, metadata.GetDeformationStateDataSize()));
    }
    if (vertexAlphaSize > 0 && alphaBuffer->storageMode() == MTL::StorageModeManaged) {
      alphaBuffer->didModifyRange(NS::Range(0, vertexAlphaSize));
    }
  }
  
  blitEncoder->copyFromBuffer(
      textureStagingBuffer.get(), /*sourceOffset*/ 0, /*sourceBytesPerRow*/ metadata.textureWidth,
      /*sourceBytesPerImage*/ metadata.textureWidth * metadata.textureHeight, /*sourceSize*/ MTL::Size(metadata.textureWidth, metadata.textureHeight, 1),
      textureLuma.get(), /*destinationSlice*/ 0, /*destinationLevel*/ 0, /*destinationOrigin*/ MTL::Origin(0, 0, 0));
  
  blitEncoder->copyFromBuffer(
      textureStagingBuffer.get(), /*sourceOffset*/ metadata.textureWidth * metadata.textureHeight, /*sourceBytesPerRow*/ metadata.textureWidth / 2,
      /*sourceBytesPerImage*/ (metadata.textureWidth * metadata.textureHeight) / 4, /*sourceSize*/ MTL::Size(metadata.textureWidth / 2, metadata.textureHeight / 2, 1),
      textureChromaU.get(), /*destinationSlice*/ 0, /*destinationLevel*/ 0, /*destinationOrigin*/ MTL::Origin(0, 0, 0));
  
  blitEncoder->copyFromBuffer(
      textureStagingBuffer.get(), /*sourceOffset*/ (metadata.textureWidth * metadata.textureHeight * 5) / 4, /*sourceBytesPerRow*/ metadata.textureWidth / 2,
      /*sourceBytesPerImage*/ (metadata.textureWidth * metadata.textureHeight) / 4, /*sourceSize*/ MTL::Size(metadata.textureWidth / 2, metadata.textureHeight / 2, 1),
      textureChromaV.get(), /*destinationSlice*/ 0, /*destinationLevel*/ 0, /*destinationOrigin*/ MTL::Origin(0, 0, 0));
  
  blitEncoder->endEncoding();
  
  blitCompleteSync->blitComplete = false;
  cmdBuf->addCompletedHandler([this](MTL::CommandBuffer* /*cmdBuf*/) {
    {
      lock_guard<mutex> lock(blitCompleteSync->blitCompleteMutex);
      blitCompleteSync->blitComplete = true;
    }
    blitCompleteSync->blitCompleteCondition.notify_all();
  });
  cmdBuf->commit();
  
  if (verboseDecoding) {
    const TimePoint frameLoadingEndTime = Clock::now();
    LOG(1) << "MetalXRVideoFrame: Frame loading time without I/O, with texture wait: " << (MillisecondsDuration(frameLoadingEndTime - frameLoadingStartTime).count()) << " ms";
  }
  return true;
}

void MetalXRVideoFrame::Destroy() {
  verticesStagingBuffer.reset();
  indicesStagingBuffer.reset();
  
  storageStagingBuffer.reset();
  alphaStagingBuffer.reset();
  textureStagingBuffer.reset();
  
  vertexBuffer.reset();
  indexBuffer.reset();
  storageBuffer.reset();
  alphaBuffer.reset();
  
  textureLuma.reset();
  textureChromaU.reset();
  textureChromaV.reset();
}

void MetalXRVideoFrame::WaitForResourceTransfers() {
  {
    unique_lock<mutex> lock(blitCompleteSync->blitCompleteMutex);
    while (!blitCompleteSync->blitComplete) {
      blitCompleteSync->blitCompleteCondition.wait(lock);
    }
  }
  
  verticesStagingBuffer.reset();
  indicesStagingBuffer.reset();
  
  storageStagingBuffer.reset();
  alphaStagingBuffer.reset();
  textureStagingBuffer.reset();
}

void MetalXRVideoFrame::Render(
   MTL::RenderCommandEncoder* encoder,
   bool flipBackFaceCulling,
   bool useSurfaceNormalShading,
   XRVideo_Vertex_InstanceData* instanceData,
   u32 interpolatedDeformationStateBufferIndex,
   const MetalXRVideoFrame* lastKeyframe,
   const MetalXRVideo* xrVideo) const {
  const MetalXRVideoFrame* baseFrame = metadata.isKeyframe ? this : lastKeyframe;
  const auto& baseFrameMetadata = baseFrame->metadata;
  if (baseFrameMetadata.indexCount == 0) {
    return;
  }
  
  // Update the uniform buffers
  instanceData->bboxMin = {baseFrameMetadata.bboxMinX, baseFrameMetadata.bboxMinY, baseFrameMetadata.bboxMinZ, 0};
  instanceData->vertexFactor = {baseFrameMetadata.vertexFactorX, baseFrameMetadata.vertexFactorY, baseFrameMetadata.vertexFactorZ, 0};
  
  XRVideo_Fragment_InstanceData fragmentInstanceData;
  fragmentInstanceData.textureSize = {static_cast<float>(textureLuma->width()), static_cast<float>(textureLuma->height())};
  
  // Render the mesh
  if (metadata.hasVertexAlpha) {
    // TODO
    LOG(ERROR) << "The metal render path does not support rendering videos with transparency yet";
  }
  const MTL::RenderPipelineState* renderPipeline = useSurfaceNormalShading ? nullptr : xrVideo->CommonResources()->pipeline.get();
  
  encoder->setCullMode(MTL::CullModeBack);
  encoder->setFrontFacingWinding(flipBackFaceCulling ? MTL::WindingClockwise : MTL::WindingCounterClockwise);
  encoder->setDepthStencilState(xrVideo->CommonResources()->depthStencilState.get());
  
  encoder->setRenderPipelineState(renderPipeline);
  
  encoder->setVertexBuffer(baseFrame->vertexBuffer.get(), /*offset*/ 0, XRVideo_VertexInputIndex_vertices);
  encoder->setVertexBytes(instanceData, sizeof(*instanceData), XRVideo_VertexInputIndex_instanceData);
  encoder->setVertexBuffer(xrVideo->interpolatedDeformationStates[interpolatedDeformationStateBufferIndex].get(), /*offset*/ 0, XRVideo_VertexInputIndex_deformationState);
  
  encoder->setFragmentBytes(&fragmentInstanceData, sizeof(fragmentInstanceData), XRVideo_FragmentInputIndex_instanceData);
  encoder->setFragmentTexture(textureLuma.get(), XRVideo_FragmentTextureInputIndex_textureLuma);
  encoder->setFragmentTexture(textureChromaU.get(), XRVideo_FragmentTextureInputIndex_textureChromaU);
  encoder->setFragmentTexture(textureChromaV.get(), XRVideo_FragmentTextureInputIndex_textureChromaV);
  
  encoder->drawIndexedPrimitives(
      MTL::PrimitiveType::PrimitiveTypeTriangle, baseFrameMetadata.indexCount, MTL::IndexTypeUInt16, baseFrame->indexBuffer.get(), /*indexBufferOffset*/ 0, /*instanceCount*/ 1);
}

}

#endif
