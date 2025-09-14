#ifdef HAVE_VULKAN
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo.hpp"

#include <libvis/vulkan/barrier.h>
#include <libvis/vulkan/command_buffer.h>

#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_common_resources.hpp"
#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

VulkanXRVideo::VulkanXRVideo(
    int viewCount,
    int framesInFlightCount,
    VulkanDevice* device)
    : viewCount(viewCount),
      framesInFlightCount(framesInFlightCount),
      device(device) {}

VulkanXRVideo::~VulkanXRVideo() {
  Destroy();
}

void VulkanXRVideo::Destroy() {
  readingThread.RequestThreadToExit();
  videoThread.RequestThreadToExit();
  decodingThread.RequestThreadToExit();
  transferThread.RequestThreadToExit();
  
  descriptorSets.clear();
  descriptorPool.Destroy();
  uniformBuffersVertex.clear();
  
  delayedFrameLockReleaseQueue.DeleteAll();
  framesLockedForRendering = vector<ReadLockedCachedFrame<VulkanXRVideoFrame>>();
  
  readingThread.WaitForThreadToExit();
  videoThread.WaitForThreadToExit();
  decodingThread.WaitForThreadToExit();
  transferThread.WaitForThreadToExit();
  
  decodingThread.Destroy();
  transferThread.Destroy(/*finishAllTransfers*/ true);
  
  decodedFrameCache.Destroy();
  
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
}

unique_ptr<XRVideoRenderLock> VulkanXRVideo::CreateRenderLock() {
  decodedFrameCache.Lock();
  
  if (framesLockedForRendering.empty()) {
    decodedFrameCache.Unlock();
    return nullptr;
  }
  
  auto result = unique_ptr<XRVideoRenderLock>(
      new VulkanXRVideoRenderLock(
          this,
          currentIntraFrameTime,
          // copy (rather than move) the framesLockedForRendering into the created render lock object
          vector<ReadLockedCachedFrame<VulkanXRVideoFrame>>(framesLockedForRendering)));
  decodedFrameCache.Unlock();
  
  return result;
}

bool VulkanXRVideo::InitializeImpl() {
  // Initialize uniform buffers
  uniformBuffersVertex.resize(viewCount * framesInFlightCount);
  for (int i = 0; i < uniformBuffersVertex.size(); ++ i) {
    if (!uniformBuffersVertex[i].InitializeAndMapUniformBuffer(max(sizeof(UniformBufferDataVertex), sizeof(UniformBufferDataVertex_NormalShading)), *device)) {
      LOG(ERROR) << "Failed to initialize a uniform buffer";
      Destroy(); return false;
    }
    uniformBuffersVertex[i].SetDebugNameIfDebugging("uniformBuffersVertex");
  }
  
  // Initialize storage buffers
  // NOTE: We allocate the maximum size for this, however, this may waste GPU memory.
  //       We could start with a smaller allocation and only grow it if needed.
  //       This is not a priority though: the space needed is only 1.1249 MiB.
  const u32 maxDeformationNodeCount = pow(2, 13) - 1;
  const u32 maxDeformationStateSizeAligned = AlignToMultipleOf<u32>(maxDeformationNodeCount * 12 * sizeof(float), device->physical_device().properties().limits.minStorageBufferOffsetAlignment);
  const u32 storageBufferSize = framesInFlightCount * maxDeformationStateSizeAligned;
  
  if (!interpolatedDeformationStates.InitializeDeviceLocal(storageBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, *device)) {
    LOG(ERROR) << "Failed to initialize a storage buffer";
    Destroy(); return false;
  }
  interpolatedDeformationStates.SetDebugNameIfDebugging("interpolatedDeformationStates");
  
  interpolatedDeformationStateBuffers.resize(framesInFlightCount);
  for (int i = 0; i < framesInFlightCount; ++ i) {
    interpolatedDeformationStateBuffers[i] = VkDescriptorBufferInfo{};
    interpolatedDeformationStateBuffers[i].buffer = interpolatedDeformationStates.descriptor().buffer;
    interpolatedDeformationStateBuffers[i].offset = i * maxDeformationStateSizeAligned;
    interpolatedDeformationStateBuffers[i].range = maxDeformationStateSizeAligned;
  }
  
  // Initialize descriptor pool and descriptor sets
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, viewCount * framesInFlightCount);
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * viewCount * framesInFlightCount);
  descriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * viewCount * framesInFlightCount);
  if (!descriptorPool.Initialize(/*max_sets*/ viewCount * framesInFlightCount, *device)) {
    LOG(ERROR) << "Failed to allocate a descriptor pool";
    Destroy(); return false;
  }
  
  descriptorSets.resize(viewCount * framesInFlightCount);
  for (auto i = 0; i < descriptorSets.size(); ++ i) {
    auto& descriptorSet = descriptorSets[i];
    
    if (!descriptorSet.Initialize(CommonResources()->descriptorSetLayout, descriptorPool, *device)) {
      LOG(ERROR) << "Failed to initialize a descriptor set";
      Destroy(); return false;
    }
    
    descriptorSet.UpdateVertexUBO(&uniformBuffersVertex[i].descriptor());
  }
  
  return true;
}

bool VulkanXRVideo::ResizeDecodedFrameCache(int cachedDecodedFrameCount) {
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  framesLockedForRendering.clear();
  
  decodedFrameCache.Initialize(cachedDecodedFrameCount);
  for (int cacheItemIndex = 0; cacheItemIndex < cachedDecodedFrameCount; ++ cacheItemIndex) {
    WriteLockedCachedFrame<VulkanXRVideoFrame> lockedFrame = decodedFrameCache.LockCacheItemForWriting(cacheItemIndex);
    lockedFrame.GetFrame()->Configure(device);
    if (allocateExternalFrameResourcesCallback) {
      if (!allocateExternalFrameResourcesCallback(cacheItemIndex, lockedFrame.GetFrame())) {
        return false;
      }
    }
  }
  
  return true;
}


void VulkanXRVideoRenderLock::PrepareFrame(RenderState* renderState) {
  auto* commonResources = Video()->CommonResources();
  auto& currentFrame = Video()->currentFrame;
  
  ++ currentFrame;
  
  if (framesLockedForRendering.empty()) { return; }
  
  VulkanRenderState* vulkanRenderState = renderState->AsVulkanRenderState();
  VulkanCommandBuffer* cmdBuf = vulkanRenderState->cmdBuf;
  
  VulkanXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  VulkanXRVideoFrame* predecessorFrame = (framesLockedForRendering.size() == 1) ? nullptr : framesLockedForRendering.back().GetFrame();
  
  currentFrameInFlightIndex = vulkanRenderState->frameInFlightIndex;
  
  // Ensure that the XRVideo frames used in rendering stay read-locked while the current rendered frame is in flight
  Video()->delayedFrameLockReleaseQueue.SetFrameIndex(currentFrame);
  Video()->decodedFrameCache.Lock();
  auto readLockCopies = make_shared<vector<ReadLockedCachedFrame<VulkanXRVideoFrame>>>(framesLockedForRendering);
  Video()->decodedFrameCache.Unlock();
  Video()->delayedFrameLockReleaseQueue.AddForDeletion(currentFrame + vulkanRenderState->framesInFlightCount, readLockCopies);
  
  // Run a compute shader to interpolate the deformation state for display
  frame->EnsureResourcesAreInGraphicsQueueFamily(cmdBuf);
  
  // Wait with dispatching until the last frame's rendering has finished
  // TODO: Is that really necessary? Shouldn't the use of a separate buffer for each frame in flight achieve that already?
  //       However, this barrier is necessary to avoid a SYNC-HAZARD-WRITE-AFTER-READ Vulkan validation error.
  VulkanMemoryBarrier(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
      .Submit(*cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  
  if (frame->GetMetadata().isKeyframe) {
    const u32 coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      // Interpolate from identity to the keyframe's deformation state.
      commonResources->interpolateDeformationStateFromIdentity.GetDescriptorSet0(currentFrameInFlightIndex).Update(
          /*State*/ &frame->GetDeformationStateBufferDesc(),
          /*Output*/ &Video()->interpolatedDeformationStateBuffers[currentFrameInFlightIndex]);
      
      commonResources->interpolateDeformationStateFromIdentity.CmdUpdatePushConstants(*cmdBuf, currentIntraFrameTime, coefficientCount);
      commonResources->interpolateDeformationStateFromIdentity.CmdDispatchOn1DDomain(*cmdBuf, coefficientCount, currentFrameInFlightIndex);
    }
  } else {
    // Interpolate from the previous frame's deformation state to the current frame's deformation state.
    predecessorFrame->EnsureResourcesAreInGraphicsQueueFamily(cmdBuf);
    
    if (frame->GetMetadata().deformationNodeCount != predecessorFrame->GetMetadata().deformationNodeCount) {
      LOG(ERROR) << "Inconsistency in deformationNodeCount between current and previous frame";
    }
    
    const u32 coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      commonResources->interpolateDeformationState.GetDescriptorSet0(currentFrameInFlightIndex).Update(
          /*State1*/ &predecessorFrame->GetDeformationStateBufferDesc(),
          /*State2*/ &frame->GetDeformationStateBufferDesc(),
          /*Output*/ &Video()->interpolatedDeformationStateBuffers[currentFrameInFlightIndex]);
      
      commonResources->interpolateDeformationState.CmdUpdatePushConstants(*cmdBuf, currentIntraFrameTime, coefficientCount);
      commonResources->interpolateDeformationState.CmdDispatchOn1DDomain(*cmdBuf, coefficientCount, currentFrameInFlightIndex);
    }
  }
  
  VulkanMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
      .Submit(*cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
}

void VulkanXRVideoRenderLock::PrepareView(int viewIndex, bool /*flipBackFaceCulling*/, bool useSurfaceNormalShading, RenderState* renderState) {
  if (framesLockedForRendering.empty()) { return; }
  VulkanCommandBuffer* cmdBuf = renderState->AsVulkanRenderState()->cmdBuf;
  
  VulkanXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  VulkanXRVideoFrame* baseKeyframe = (framesLockedForRendering.size() == 1) ? framesLockedForRendering[0].GetFrame() : framesLockedForRendering[1].GetFrame();
  
  currentView = viewIndex;
  this->useSurfaceNormalShading = useSurfaceNormalShading;
  
  frame->PrepareRender(
      cmdBuf,
      &Video()->descriptorSets[Video()->GetFrameResourceIndex(currentView, currentFrameInFlightIndex)],
      Video()->interpolatedDeformationStateBuffers[currentFrameInFlightIndex],
      baseKeyframe);
}

void VulkanXRVideoRenderLock::RenderView(RenderState* renderState) {
  if (framesLockedForRendering.empty()) { return; }
  
  const VulkanXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  const VulkanXRVideoFrame* baseKeyframe = (framesLockedForRendering.size() == 1) ? framesLockedForRendering[0].GetFrame() : framesLockedForRendering[1].GetFrame();
  
  int frameResourceIndex = Video()->GetFrameResourceIndex(currentView, currentFrameInFlightIndex);
  
  frame->Render(
      renderState->AsVulkanRenderState()->cmdBuf, useSurfaceNormalShading,
      &Video()->descriptorSets[frameResourceIndex], &Video()->uniformBuffersVertex[frameResourceIndex], baseKeyframe, Video());
}

void VulkanXRVideoRenderLock::SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) {
  if (framesLockedForRendering.empty()) { return; }
  
  if (multiViewIndex != 0) {
    LOG(ERROR) << "Single-pass multi-view rendering is not supported in the Vulkan render path yet";
    return;
  }
  
  const int frameResourceIndex = Video()->GetFrameResourceIndex(viewIndex, currentFrameInFlightIndex);
  
  if (useSurfaceNormalShading) {
    memcpy(
        Video()->uniformBuffersVertex[frameResourceIndex].data<VulkanXRVideo::UniformBufferDataVertex_NormalShading>()->modelView.data(),
        columnMajorModelViewData,
        4 * 4 * sizeof(float));
    memcpy(
        Video()->uniformBuffersVertex[frameResourceIndex].data<VulkanXRVideo::UniformBufferDataVertex_NormalShading>()->modelViewProjection.data(),
        columnMajorModelViewProjectionData,
        4 * 4 * sizeof(float));
  } else {
    memcpy(
        Video()->uniformBuffersVertex[frameResourceIndex].data<VulkanXRVideo::UniformBufferDataVertex>()->modelViewProjection.data(),
        columnMajorModelViewProjectionData,
        4 * 4 * sizeof(float));
  }
}

}

#endif
