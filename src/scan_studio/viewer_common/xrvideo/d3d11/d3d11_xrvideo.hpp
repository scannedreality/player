#pragma once
#ifdef HAVE_D3D11

#include <memory>

#include <d3d11_4.h>

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo_frame.hpp"

namespace scan_studio {
using namespace vis;

class D3D11XRVideoCommonResources;

/// XRVideo rendering implementation using D3D11.
class D3D11XRVideo : public XRVideoImpl<D3D11XRVideoFrame> {
 friend class D3D11XRVideoFrame;
 friend class D3D11XRVideoRenderLock;
 public:
  D3D11XRVideo(
      int framesInFlightCount,
      ID3D11DeviceContext4* deviceContext,
      ID3D11Device5* device);
  ~D3D11XRVideo();
  
  void UseExternalResourceMode(int interpolatedDeformationStateBufferCount, ID3D11Buffer** interpolatedDeformationStateBuffers);
  
  virtual void Destroy() override;
  
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() override;
  
 protected:
  bool InitializeImpl() override;
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) override;
  
 private:
  inline const D3D11XRVideoCommonResources* CommonResources() const { return reinterpret_cast<D3D11XRVideoCommonResources*>(commonResources); }
  inline D3D11XRVideoCommonResources* CommonResources() { return reinterpret_cast<D3D11XRVideoCommonResources*>(commonResources); }
  
  u32 currentFrameInFlightIndex = 0;
  
  /// Storage buffers for holding interpolated deformation states (for each frame in flight)
  vector<shared_ptr<ID3D11Buffer>> interpolatedDeformationStates;
  vector<shared_ptr<ID3D11UnorderedAccessView>> interpolatedDeformationStateViews;
  
  /// Constant buffer for invoking the compute shader
  struct InterpolateDeformationStateConstantBuffer {
    float factor;
    u32 coefficientCount;
  };
  shared_ptr<ID3D11Buffer> interpolatedDeformationStateCB;
  
  bool externalResourceMode = false;
  
  int framesInFlightCount;
  shared_ptr<ID3D11Device5> device;
  shared_ptr<ID3D11DeviceContext4> deviceContext;
};

class D3D11XRVideoRenderLock : public XRVideoRenderLockImpl<D3D11XRVideoFrame> {
 friend class D3D11XRVideo;
 public:
  virtual void PrepareFrame(RenderState* renderState) override;
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) override;
  virtual void RenderView(RenderState* renderState) override;
  virtual inline bool SupportsLateModelViewProjectionSetting() override { return true; }
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) override;
  
  virtual inline int GetDeformationStateResourceIndex() const override { return currentFrameInFlightIndex; }
  
 protected:
  inline D3D11XRVideoRenderLock(
      D3D11XRVideo* video,
      u32 currentFrameInFlightIndex,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<D3D11XRVideoFrame>>&& framesLockedForRendering)
      : XRVideoRenderLockImpl<D3D11XRVideoFrame>(video, currentIntraFrameTime, std::move(framesLockedForRendering)),
        currentFrameInFlightIndex(currentFrameInFlightIndex) {}
  
  inline const D3D11XRVideo* Video() const { return reinterpret_cast<D3D11XRVideo*>(video); }
  inline D3D11XRVideo* Video() { return reinterpret_cast<D3D11XRVideo*>(video); }
  
  int currentView;
  u32 currentFrameInFlightIndex;
  // XRVideo_Vertex_InstanceData instanceData;
};

}

#endif
