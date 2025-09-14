#pragma once

#include <memory>

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/external/external_xrvideo_frame.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

#include "scan_studio/player_library/scannedreality_player.h"

namespace scan_studio {
using namespace vis;

/// XRVideo implementation that does not render the video directly;
/// instead, it passes the (decoded) raw data to external callbacks.
class ExternalXRVideo : public XRVideoImpl<ExternalXRVideoFrame> {
 friend class ExternalXRVideoFrame;
 friend class ExternalXRVideoRenderLock;
 public:
  ExternalXRVideo(SRPlayer_XRVideo_External_Config callbacks);
  ~ExternalXRVideo();
  
  virtual void Destroy() override;
  
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() override;
  
 protected:
  bool InitializeImpl() override;
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) override;
  
 private:
  SRPlayer_XRVideo_External_Config callbacks;
};

class ExternalXRVideoRenderLock : public XRVideoRenderLockImpl<ExternalXRVideoFrame> {
 friend class ExternalXRVideo;
 public:
  virtual void PrepareFrame(RenderState* renderState) override;
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) override;
  virtual void RenderView(RenderState* renderState) override;
  virtual inline bool SupportsLateModelViewProjectionSetting() override { return true; }
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) override;
  
 protected:
  inline ExternalXRVideoRenderLock(
      ExternalXRVideo* video,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<ExternalXRVideoFrame>>&& framesLockedForRendering)
      : XRVideoRenderLockImpl<ExternalXRVideoFrame>(video, currentIntraFrameTime, std::move(framesLockedForRendering)) {}
  
  inline const ExternalXRVideo* Video() const { return reinterpret_cast<ExternalXRVideo*>(video); }
  inline ExternalXRVideo* Video() { return reinterpret_cast<ExternalXRVideo*>(video); }
};

}
