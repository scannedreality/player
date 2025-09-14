#pragma once
#ifdef HAVE_VULKAN

#include <memory>

#include <Eigen/Core>

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/command_pool.h>
#include <libvis/vulkan/delayed_delete_queue.h>
#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/device.h>
#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/pipeline.h>
#include <libvis/vulkan/render_pass.h>

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_frame.hpp"

namespace scan_studio {
using namespace vis;

class VulkanXRVideoCommonResources;

/// XRVideo rendering implementation using Vulkan.
class VulkanXRVideo : public XRVideoImpl<VulkanXRVideoFrame> {
 friend class VulkanXRVideoFrame;
 friend class VulkanXRVideoRenderLock;
 public:
  VulkanXRVideo(
      int viewCount,
      int framesInFlightCount,
      VulkanDevice* device);
  ~VulkanXRVideo();
  
  virtual void Destroy() override;
  
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() override;
  
 protected:
  bool InitializeImpl() override;
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) override;
  
 private:
  inline const VulkanXRVideoCommonResources* CommonResources() const { return reinterpret_cast<VulkanXRVideoCommonResources*>(commonResources); }
  inline VulkanXRVideoCommonResources* CommonResources() { return reinterpret_cast<VulkanXRVideoCommonResources*>(commonResources); }
  
  inline int GetFrameResourceIndex(int viewIndex, int frameInFlightIndex) const { return viewIndex + frameInFlightIndex * viewCount; }
  
  int viewCount;
  
  u32 currentFrame = 0;
  
  // Uniform buffers for the vertex shader (for each frame in flight)
  #pragma pack(push, 1)
  struct UniformBufferDataVertex {
    Eigen::Matrix4f modelViewProjection;
    float invTextureWidth;
    float invTextureHeight;
    float bboxMinX, bboxMinY, bboxMinZ;
    float vertexFactorX, vertexFactorY, vertexFactorZ;
  };
  struct UniformBufferDataVertex_NormalShading {
    Eigen::Matrix4f modelView;
    Eigen::Matrix4f modelViewProjection;
    float invTextureWidth;
    float invTextureHeight;
    float bboxMinX, bboxMinY, bboxMinZ;
    float vertexFactorX, vertexFactorY, vertexFactorZ;
  };
  #pragma pack(pop)
  vector<VulkanBuffer> uniformBuffersVertex;
  
  // Storage buffers for holding interpolated deformation states (for each frame in flight)
  VulkanBuffer interpolatedDeformationStates;
  vector<VkDescriptorBufferInfo> interpolatedDeformationStateBuffers;
  
  // Descriptor sets referencing the uniform buffer (for each frame in flight)
  vector<vulkan_xrvideo_shader::DescriptorSet0> descriptorSets;
  VulkanDescriptorPool descriptorPool;
  
  /// Delayed delete queue for Vulkan objects
  VulkanDelayedDeleteQueue delayedFrameLockReleaseQueue;
  
  int framesInFlightCount;
  VulkanDevice* device;  // not owned
};

class VulkanXRVideoRenderLock : public XRVideoRenderLockImpl<VulkanXRVideoFrame> {
 friend class VulkanXRVideo;
 public:
  virtual void PrepareFrame(RenderState* renderState) override;
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) override;
  virtual void RenderView(RenderState* renderState) override;
  virtual inline bool SupportsLateModelViewProjectionSetting() override { return true; }
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) override;
  
 protected:
  inline VulkanXRVideoRenderLock(
      VulkanXRVideo* video,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<VulkanXRVideoFrame>>&& framesLockedForRendering)
      : XRVideoRenderLockImpl<VulkanXRVideoFrame>(video, currentIntraFrameTime, std::move(framesLockedForRendering)) {}
  
  inline const VulkanXRVideo* Video() const { return reinterpret_cast<VulkanXRVideo*>(video); }
  inline VulkanXRVideo* Video() { return reinterpret_cast<VulkanXRVideo*>(video); }
  
  int currentView;
  u32 currentFrameInFlightIndex;
};

}

#endif
