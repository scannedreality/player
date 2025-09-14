#pragma once
#ifdef __APPLE__

#include <memory>

#include <Metal/Metal.hpp>

#include <Eigen/Core>

#include <libvis/vulkan/delayed_delete_queue.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_frame.hpp"

#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_shader.h"

namespace scan_studio {
using namespace vis;

class MetalXRVideoCommonResources;

/// XRVideo rendering implementation using Metal.
class MetalXRVideo : public XRVideoImpl<MetalXRVideoFrame> {
 friend class MetalXRVideoFrame;
 friend class MetalXRVideoRenderLock;
 public:
  MetalXRVideo(
      int viewCount,
      int framesInFlightCount,
      MTL::Device* device);
  ~MetalXRVideo();
  
  void UseExternalResourceMode(int interpolatedDeformationStateBufferCount, MTL::Buffer** interpolatedDeformationStateBuffers);
  
  virtual void Destroy() override;
  
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() override;
  
 protected:
  bool InitializeImpl() override;
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) override;
  
 private:
  inline const MetalXRVideoCommonResources* CommonResources() const { return reinterpret_cast<MetalXRVideoCommonResources*>(commonResources); }
  inline MetalXRVideoCommonResources* CommonResources() { return reinterpret_cast<MetalXRVideoCommonResources*>(commonResources); }
  
  int viewCount;
  
  u32 currentFrameInFlightIndex = 0;
  
  /// Storage buffers for holding interpolated deformation states (for each frame in flight)
  vector<NS::SharedPtr<MTL::Buffer>> interpolatedDeformationStates;
  
  /// Dedicated command queue for transfer operations
  NS::SharedPtr<MTL::CommandQueue> transferCommandQueue;
  
  bool externalResourceMode = false;
  
  int framesInFlightCount;
  MTL::Device* device;  // not owned
};

class MetalXRVideoRenderLock : public XRVideoRenderLockImpl<MetalXRVideoFrame> {
 friend class MetalXRVideo;
 public:
  virtual void PrepareFrame(RenderState* renderState) override;
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) override;
  virtual void RenderView(RenderState* renderState) override;
  virtual inline bool SupportsLateModelViewProjectionSetting() override { return true; }
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) override;
  
  virtual inline int GetDeformationStateResourceIndex() const override { return currentFrameInFlightIndex; }
  
 protected:
  inline MetalXRVideoRenderLock(
      MetalXRVideo* video,
      u32 currentFrameInFlightIndex,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<MetalXRVideoFrame>>&& framesLockedForRendering)
      : XRVideoRenderLockImpl<MetalXRVideoFrame>(video, currentIntraFrameTime, std::move(framesLockedForRendering)),
        currentFrameInFlightIndex(currentFrameInFlightIndex) {}
  
  inline const MetalXRVideo* Video() const { return reinterpret_cast<MetalXRVideo*>(video); }
  inline MetalXRVideo* Video() { return reinterpret_cast<MetalXRVideo*>(video); }
  
  int currentView;
  u32 currentFrameInFlightIndex;
  XRVideo_Vertex_InstanceData instanceData;
};

}

#endif
