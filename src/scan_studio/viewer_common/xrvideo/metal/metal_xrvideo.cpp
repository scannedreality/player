#ifdef __APPLE__
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo.hpp"

#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_common_resources.hpp"
#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/xrvideo/metal/metal_interpolate_deformation_state.h"

namespace scan_studio {

MetalXRVideo::MetalXRVideo(
    int viewCount,
    int framesInFlightCount,
    MTL::Device* device)
    : viewCount(viewCount),
      framesInFlightCount(framesInFlightCount),
      device(device) {}

MetalXRVideo::~MetalXRVideo() {
  Destroy();
}

void MetalXRVideo::UseExternalResourceMode(int interpolatedDeformationStateBufferCount, MTL::Buffer** interpolatedDeformationStateBuffers) {
  interpolatedDeformationStates.resize(interpolatedDeformationStateBufferCount);
  
  for (int i = 0; i < interpolatedDeformationStateBufferCount; ++ i) {
    interpolatedDeformationStates[i] = NS::RetainPtr(interpolatedDeformationStateBuffers[i]);
  }
  
  externalResourceMode = true;
}

void MetalXRVideo::Destroy() {
  readingThread.RequestThreadToExit();
  videoThread.RequestThreadToExit();
  decodingThread.RequestThreadToExit();
  transferThread.RequestThreadToExit();
  
  framesLockedForRendering = vector<ReadLockedCachedFrame<MetalXRVideoFrame>>();
  
  readingThread.WaitForThreadToExit();
  videoThread.WaitForThreadToExit();
  decodingThread.WaitForThreadToExit();
  transferThread.WaitForThreadToExit();
  
  decodingThread.Destroy();
  transferThread.Destroy(/*finishAllTransfers*/ true);
  
  decodedFrameCache.Destroy();
  
  transferCommandQueue.reset();
  
  interpolatedDeformationStates.clear();
  
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
}

unique_ptr<XRVideoRenderLock> MetalXRVideo::CreateRenderLock() {
  decodedFrameCache.Lock();
  
  if (framesLockedForRendering.empty()) {
    decodedFrameCache.Unlock();
    return nullptr;
  }
  
  auto result = unique_ptr<XRVideoRenderLock>(
      new MetalXRVideoRenderLock(
          this,
          currentFrameInFlightIndex,
          currentIntraFrameTime,
          // copy (rather than move) the framesLockedForRendering into the created render lock object
          vector<ReadLockedCachedFrame<MetalXRVideoFrame>>(framesLockedForRendering)));
  
  currentFrameInFlightIndex = (currentFrameInFlightIndex + 1) % framesInFlightCount;
  
  decodedFrameCache.Unlock();
  return result;
}

bool MetalXRVideo::InitializeImpl() {
  // Initialize storage buffers (or only check for their correct size in external resource mode).
  // NOTE: We allocate the maximum size for this, however, this may waste GPU memory.
  //       We could start with a smaller allocation and only grow it if needed.
  //       This is not a priority though: the space needed is only 1.1249 MiB.
  // NOTE: For vertex buffer alignment requirements, see:
  //       https://developer.apple.com/documentation/metal/mtlrendercommandencoder/1515829-setvertexbuffer
  //       256 bytes is always safe.
  // TODO: I am not sure whether it is really helpful to have a separate buffer for each frame-in-flight here.
  //       If the GPU only starts the deformation state interpolation after the previous frame has finished rendering,
  //       then it would be like for the Z-buffer depth image, where only a single resource is required.
  const u32 maxDeformationNodeCount = pow(2, 13) - 1;
  const u32 storageBufferSize = maxDeformationNodeCount * 12 * sizeof(float);
  
  if (externalResourceMode) {
    if (interpolatedDeformationStates.size() != framesInFlightCount) {
      LOG(ERROR) << "External interpolated deformation states buffers: Invalid framesInFlightCount. Actual: " << interpolatedDeformationStates.size() << ". Required: " << framesInFlightCount;
      return false;
    }
    
    for (int i = 0; i < interpolatedDeformationStates.size(); ++ i) {
      if (interpolatedDeformationStates[i]->length() < storageBufferSize) {
        LOG(ERROR) << "External interpolated deformation states buffer is too small. Available bytes: " << interpolatedDeformationStates[i]->length() << ". Required bytes: " << storageBufferSize;
        return false;
      }
    }
  } else {
    interpolatedDeformationStates.resize(framesInFlightCount);
    
    for (int i = 0; i < framesInFlightCount; ++ i) {
      interpolatedDeformationStates[i] = NS::TransferPtr(device->newBuffer(storageBufferSize, MTL::ResourceStorageModePrivate));
      if (!interpolatedDeformationStates[i]) {
        LOG(ERROR) << "Failed to initialize a storage buffer";
        Destroy(); return false;
      }
    }
  }
  
  // Create a dedicated command queue for the XRVideo's transfer operations
  transferCommandQueue = NS::TransferPtr(device->newCommandQueue());
  
  return true;
}

bool MetalXRVideo::ResizeDecodedFrameCache(int cachedDecodedFrameCount) {
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  framesLockedForRendering.clear();
  
  decodedFrameCache.Initialize(cachedDecodedFrameCount);
  for (int cacheItemIndex = 0; cacheItemIndex < cachedDecodedFrameCount; ++ cacheItemIndex) {
    WriteLockedCachedFrame<MetalXRVideoFrame> lockedFrame = decodedFrameCache.LockCacheItemForWriting(cacheItemIndex);
    lockedFrame.GetFrame()->Configure(transferCommandQueue.get(), device);
    if (allocateExternalFrameResourcesCallback) {
      if (!allocateExternalFrameResourcesCallback(cacheItemIndex, lockedFrame.GetFrame())) { return false; }
    }
  }
  
  return true;
}


void MetalXRVideoRenderLock::PrepareFrame(RenderState* renderState) {
  auto* commonResources = Video()->CommonResources();
  
  if (framesLockedForRendering.empty()) { return; }
  
  MetalRenderState* metalRenderState = renderState->AsMetalRenderState();
  
  MetalXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  MetalXRVideoFrame* predecessorFrame = (framesLockedForRendering.size() == 1) ? nullptr : framesLockedForRendering.back().GetFrame();
  
  // Ensure that the XRVideo frames used in rendering stay read-locked while the current rendered frame is in flight.
  // Notice that the copy made here is an additional copy next to the one in this MetalXRVideoRenderLock,
  // i.e., keeping the render lock alive longer than the cmdBuf runs will also keep the frames locked longer.
  Video()->decodedFrameCache.Lock();
  auto readLockCopies = make_shared<vector<ReadLockedCachedFrame<MetalXRVideoFrame>>>(framesLockedForRendering);
  Video()->decodedFrameCache.Unlock();
  metalRenderState->cmdBuf->addCompletedHandler([readLockCopies](MTL::CommandBuffer* /*cmdBuf*/) {
    readLockCopies->clear();
  });
  
  // Run a compute shader to interpolate the deformation state for display
  MTL::ComputeCommandEncoder* computeEncoder = metalRenderState->cmdBuf->computeCommandEncoder();
  
  u32 coefficientCount;
  MTL::ComputePipelineState* pipeline;
  
  if (frame->GetMetadata().isKeyframe) {
    coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      // Interpolate from identity to the keyframe's deformation state.
      pipeline = commonResources->interpolateDeformationStateFromIdentity.get();
      computeEncoder->setComputePipelineState(pipeline);
      
      computeEncoder->setBuffer(frame->GetDeformationStateBuffer(), /*offset*/ 0, InterpolateDeformationStateFromIdentity_InputIndex_state1);
      computeEncoder->setBuffer(
          Video()->interpolatedDeformationStates[currentFrameInFlightIndex].get(), /*offset*/ 0, InterpolateDeformationStateFromIdentity_InputIndex_outputState);
      const float factor = currentIntraFrameTime;
      computeEncoder->setBytes(&factor, sizeof(factor), InterpolateDeformationStateFromIdentity_InputIndex_factor);
      computeEncoder->setBytes(&coefficientCount, sizeof(coefficientCount), InterpolateDeformationStateFromIdentity_InputIndex_coefficientCount);
    }
  } else {
    // Interpolate from the previous frame's deformation state to the current frame's deformation state.
    if (frame->GetMetadata().deformationNodeCount != predecessorFrame->GetMetadata().deformationNodeCount) {
      LOG(ERROR) << "Inconsistency in deformationNodeCount between current and previous frame";
    }
    
    coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      pipeline = commonResources->interpolateDeformationState.get();
      computeEncoder->setComputePipelineState(pipeline);
      
      computeEncoder->setBuffer(predecessorFrame->GetDeformationStateBuffer(), /*offset*/ 0, InterpolateDeformationState_InputIndex_state1);
      computeEncoder->setBuffer(frame->GetDeformationStateBuffer(), /*offset*/ 0, InterpolateDeformationState_InputIndex_state2);
      computeEncoder->setBuffer(
          Video()->interpolatedDeformationStates[currentFrameInFlightIndex].get(), /*offset*/ 0, InterpolateDeformationState_InputIndex_outputState);
      const float factor = currentIntraFrameTime;
      computeEncoder->setBytes(&factor, sizeof(factor), InterpolateDeformationState_InputIndex_factor);
      computeEncoder->setBytes(&coefficientCount, sizeof(coefficientCount), InterpolateDeformationState_InputIndex_coefficientCount);
    }
  }
  
  if (coefficientCount > 0) {
    const MTL::Size threadsPerThreadgroup(pipeline->maxTotalThreadsPerThreadgroup(), 1, 1);
    const MTL::Size threadgroupsPerGrid((coefficientCount - 1) / threadsPerThreadgroup.width + 1, 1, 1);
    // Note: I used dispatchThreads() here before, but that is only compatible with newer Apple GPUs that support non-uniform threadgroup sizes.
    computeEncoder->dispatchThreadgroups(threadgroupsPerGrid, threadsPerThreadgroup);
    
    computeEncoder->endEncoding();
  }
}

void MetalXRVideoRenderLock::PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* /*renderState*/) {
  if (framesLockedForRendering.empty()) { return; }
  
  currentView = viewIndex;
  this->flipBackFaceCulling = flipBackFaceCulling;
  this->useSurfaceNormalShading = useSurfaceNormalShading;
  
  if (useSurfaceNormalShading) {
    LOG(ERROR) << "The Metal render path does not support useSurfaceNormalShading yet";
  }
}

void MetalXRVideoRenderLock::RenderView(RenderState* renderState) {
  if (framesLockedForRendering.empty()) { return; }
  
  const MetalXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  const MetalXRVideoFrame* baseKeyframe = (framesLockedForRendering.size() == 1) ? framesLockedForRendering[0].GetFrame() : framesLockedForRendering[1].GetFrame();
  
  frame->Render(
      renderState->AsMetalRenderState()->renderCmdEncoder, flipBackFaceCulling, useSurfaceNormalShading,
      &instanceData, currentFrameInFlightIndex, baseKeyframe, Video());
}

void MetalXRVideoRenderLock::SetModelViewProjection(int /*viewIndex*/, int multiViewIndex, const float* /*columnMajorModelViewData*/, const float* columnMajorModelViewProjectionData) {
  if (framesLockedForRendering.empty()) { return; }
  
  if (multiViewIndex != 0) {
    LOG(ERROR) << "Single-pass multi-view rendering is not supported in the Metal render path yet";
    return;
  }
  
  // TODO: Support useSurfaceNormalShading: copy columnMajorModelViewData as well then
  
  memcpy(&instanceData.modelViewProjection, columnMajorModelViewProjectionData, 4 * 4 * sizeof(float));
}

}

#endif
