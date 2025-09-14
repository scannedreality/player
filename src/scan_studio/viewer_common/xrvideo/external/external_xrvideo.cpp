#include "scan_studio/viewer_common/xrvideo/external/external_xrvideo.hpp"

namespace scan_studio {

ExternalXRVideo::ExternalXRVideo(SRPlayer_XRVideo_External_Config callbacks)
    : callbacks(callbacks) {}

ExternalXRVideo::~ExternalXRVideo() {
  Destroy();
}

void ExternalXRVideo::Destroy() {
  readingThread.RequestThreadToExit();
  videoThread.RequestThreadToExit();
  decodingThread.RequestThreadToExit();
  transferThread.RequestThreadToExit();
  
  framesLockedForRendering = vector<ReadLockedCachedFrame<ExternalXRVideoFrame>>();
  
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

unique_ptr<XRVideoRenderLock> ExternalXRVideo::CreateRenderLock() {
  decodedFrameCache.Lock();
  
  if (framesLockedForRendering.empty()) {
    decodedFrameCache.Unlock();
    return nullptr;
  }
  
  auto result = unique_ptr<XRVideoRenderLock>(
      new ExternalXRVideoRenderLock(
          this,
          currentIntraFrameTime,
          // copy (rather than move) the framesLockedForRendering into the created render lock object
          vector<ReadLockedCachedFrame<ExternalXRVideoFrame>>(framesLockedForRendering)));
  decodedFrameCache.Unlock();
  
  return result;
}

bool ExternalXRVideo::InitializeImpl() {
  // Nothing to do here: Since any external per-video initialization can easily be called by external code as well, we don't need to call a callback here.
  return true;
}

bool ExternalXRVideo::ResizeDecodedFrameCache(int cachedDecodedFrameCount) {
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  framesLockedForRendering.clear();
  
  decodedFrameCache.Initialize(cachedDecodedFrameCount);
  for (int cacheItemIndex = 0; cacheItemIndex < cachedDecodedFrameCount; ++ cacheItemIndex) {
    WriteLockedCachedFrame<ExternalXRVideoFrame> lockedFrame = decodedFrameCache.LockCacheItemForWriting(cacheItemIndex);
    lockedFrame.GetFrame()->Configure(this);
    if (allocateExternalFrameResourcesCallback) {
      if (!allocateExternalFrameResourcesCallback(cacheItemIndex, lockedFrame.GetFrame())) {
        return false;
      }
    }
  }
  
  return true;
}


void ExternalXRVideoRenderLock::PrepareFrame(RenderState* /*renderState*/) {
  // This is implemented externally for ExternalXRVideo, thus this implementation is empty.
}

void ExternalXRVideoRenderLock::PrepareView(int /*viewIndex*/, bool /*flipBackFaceCulling*/, bool /*useSurfaceNormalShading*/, RenderState* /*renderState*/) {
  // This is implemented externally for ExternalXRVideo, thus this implementation is empty.
}

void ExternalXRVideoRenderLock::RenderView(RenderState* /*renderState*/) {
  // This is implemented externally for ExternalXRVideo, thus this implementation is empty.
}

void ExternalXRVideoRenderLock::SetModelViewProjection(int /*viewIndex*/, int /*multiViewIndex*/, const float* /*columnMajorModelViewData*/, const float* /*columnMajorModelViewProjectionData*/) {
  // This is implemented externally for ExternalXRVideo, thus this implementation is empty.
}

}
