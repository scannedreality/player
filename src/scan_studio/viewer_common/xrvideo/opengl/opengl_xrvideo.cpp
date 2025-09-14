#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo.hpp"
#ifdef HAVE_OPENGL

#include <cmath>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten/threading.h>
#endif

#include <loguru.hpp>

#include "scan_studio/viewer_common/opengl/util.hpp"

#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_common_resources.hpp"

namespace scan_studio {

OpenGLXRVideo::OpenGLXRVideo(array<unique_ptr<GLContext>, 2>&& workerThreadContexts) {
  videoThread.SetUseDav1dZeroCopy(this);
  
  decodingThread.SetUseOpenGLContext(std::move(workerThreadContexts[0]));
  transferThread.SetUseOpenGLContext(std::move(workerThreadContexts[1]));
}

OpenGLXRVideo::~OpenGLXRVideo() {
  Destroy();
}

void OpenGLXRVideo::Destroy() {
  readingThread.RequestThreadToExit();
  videoThread.RequestThreadToExit();
  decodingThread.RequestThreadToExit();
  transferThread.RequestThreadToExit();
  
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &OpenGLXRVideo::DestroyImpl1Static, reinterpret_cast<int>(this));
  #else
    DestroyImpl1();
  #endif
  
  // For emscripten, we should absolutely avoid blocking on the main thread.
  // This is why we exclude the WaitForThreadToExit() calls here from the DestroyImpl1/2 parts that are run in emscripten_sync_run_in_main_runtime_thread().
  readingThread.WaitForThreadToExit();
  videoThread.WaitForThreadToExit();
  decodingThread.WaitForThreadToExit();
  transferThread.WaitForThreadToExit();
  
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &OpenGLXRVideo::DestroyImpl2Static, reinterpret_cast<int>(this));
  #else
    DestroyImpl2();
  #endif
  
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
}

unique_ptr<XRVideoRenderLock> OpenGLXRVideo::CreateRenderLock() {
  decodedFrameCache.Lock();
  
  if (framesLockedForRendering.empty()) {
    decodedFrameCache.Unlock();
    return nullptr;
  }
  
  auto result = unique_ptr<XRVideoRenderLock>(
      new OpenGLXRVideoRenderLock(
          this,
          currentIntraFrameTime,
          // copy (rather than move) the framesLockedForRendering into the created render lock object
          vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>>(framesLockedForRendering)));
  decodedFrameCache.Unlock();
  
  return result;
}

int OpenGLXRVideo::Dav1dAllocPictureCallback(Dav1dPicture* pic) {
  const usize textureSize = (dav1dZeroCopyVideoWidth * dav1dZeroCopyVideoHeight * 3) / 2;
  if (textureSize % DAV1D_PICTURE_ALIGNMENT != 0) {
    LOG(ERROR) << "The texture size must be a multiple of DAV1D_PICTURE_ALIGNMENT";
  }
  
  OpenGLXRVideoDav1dPicture picture = dav1dPictureCache.TakeOrAllocate(/*alignment*/ DAV1D_PICTURE_ALIGNMENT, /*allocationSize*/ textureSize + DAV1D_PICTURE_ALIGNMENT);
  if (picture.textureDataPtr == nullptr) {
    return DAV1D_ERR(ENOMEM);
  }
  
  pic->allocator_data = picture.textureDataPtr;
  
  pic->stride[0] = dav1dZeroCopyVideoWidth;
  pic->stride[1] = dav1dZeroCopyVideoWidth / 2;
  
  pic->data[0] = picture.textureDataPtr;
  pic->data[1] = static_cast<u8*>(picture.textureDataPtr) + (dav1dZeroCopyVideoWidth * dav1dZeroCopyVideoHeight);
  pic->data[2] = static_cast<u8*>(picture.textureDataPtr) + (dav1dZeroCopyVideoWidth * dav1dZeroCopyVideoHeight * 5) / 4;
  
  picture.Release();
  return 0;
}

void OpenGLXRVideo::Dav1dReleasePictureCallback(Dav1dPicture* pic) {
  OpenGLXRVideoDav1dPicture picture(pic->allocator_data);
  dav1dPictureCache.PutBack(std::move(picture));
}

bool OpenGLXRVideo::InitializeImpl() {
  // Initialize the texture used to store the interpolated deformation state.
  // NOTE: We allocate the maximum size for this, however, this may waste GPU memory.
  //       We could start with a smaller allocation and only grow it if needed.
  //       This is not a priority though: the space needed is only 1.1249 MiB.
  // NOTE: We used to query GL_MAX_TEXTURE_SIZE for the maximum allowed texture dimensions here.
  //       However, when writing the Unity plugin, this caused issues as Unity seems to have a custom artificial limit:
  //       https://forum.unity.com/threads/texture2d-has-out-of-range-width.501936/
  //       Fortunately, we can instead simply assume 2048 as a conservative value.
  //       This should be widely supported (GL_MAX_TEXTURE_SIZE must be at least 2048),
  //       and at the same time, 2048 * 2048 is (much) more than large enough to store
  //       the data for our current maxDeformationNodeCount.
  const GLint maxTextureSize = 2048;
  // gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  
  const u32 maxDeformationNodeCount = pow(2, 13) - 1;
  
  u32 textureWidth, textureHeight;
  ChooseTextureSizeForTexelCount(/*minTexelCount*/ 3 * maxDeformationNodeCount, /*minTextureWidth*/ 64, maxTextureSize, &textureWidth, &textureHeight);
  if (verboseDecoding) {
    LOG(1) << "Size chosen for interpolatedDeformationState is " << textureWidth << " x " << textureHeight << " (maxTextureSize: " << maxTextureSize << ")";
  }
  if (textureWidth == numeric_limits<u32>::max()) { LOG(ERROR) << "No suitable texture size found for interpolatedDeformationState"; return false; }
  interpolatedDeformationState.Allocate2D(
      textureWidth, textureHeight,
      /*GL_RGBA32UI*/ 0x8D70, /*GL_RGBA_INTEGER*/ 0x8D99, GL_UNSIGNED_INT,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_NEAREST, GL_NEAREST);
  
  // Create a framebuffer for rendering to the interpolated deformation state texture
  gl.glGenFramebuffers(1, &interpolatedDeformationStateFramebuffer);
  gl.glBindFramebuffer(GL_FRAMEBUFFER, interpolatedDeformationStateFramebuffer);
  CHECK_OPENGL_NO_ERROR();
  
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, interpolatedDeformationState.Name(), /*level*/ 0);
  CHECK_OPENGL_NO_ERROR();
  
  const GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) { return false; }
  CHECK_OPENGL_NO_ERROR();
  
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  
  framebufferInitialized = true;
  
  return true;
}

bool OpenGLXRVideo::StartLoadingThreads() {
  // Start the video, decoding and reading threads
  decodingThread.StartThread(verboseDecoding, &transferThread);
  videoThread.StartThread(verboseDecoding, &decodingThread, &index);
  readingThread.StartThread(verboseDecoding, &playbackState, &videoThread, &decodingThread, &decodedFrameCache, &asyncLoadState, &hasMetadata, &metadata, &textureWidth, &textureHeight, &index, &reader);
  
  // Start the transfer thread after waiting for the decoding thread to initialize to avoid using
  // SDL_GL_MakeCurrent() in multiple threads at the same time.
  //
  // This is unless we compile with emscripten; in that case, we don't need to wait for the threads
  // to initialize at all, since they don't call SDL_GL_MakeCurrent() in this case. At the same time,
  // this code here runs on the browser's main thread for the website in that case, which means we should
  // not block here.
  #ifndef __EMSCRIPTEN__
    if (!decodingThread.WaitForThreadToInitialize()) { return false; }
  #endif
  transferThread.StartThread(verboseDecoding);
  #ifndef __EMSCRIPTEN__
    transferThread.WaitForThreadToInitialize();
  #endif
  
  return true;
}

bool OpenGLXRVideo::ResizeDecodedFrameCache(int cachedDecodedFrameCount) {
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  framesLockedForRendering.clear();
  
  // Tell the dav1d zero-copy implementation about the video's texture size.
  if (asyncLoadState == XRVideoAsyncLoadState::Ready) {
    dav1dPictureCache.Clear();
    Configure(textureWidth, textureHeight);
  }
  
  decodedFrameCache.Initialize(cachedDecodedFrameCount);
  for (int cacheItemIndex = 0; cacheItemIndex < cachedDecodedFrameCount; ++ cacheItemIndex) {
    WriteLockedCachedFrame<OpenGLXRVideoFrame> lockedFrame = decodedFrameCache.LockCacheItemForWriting(cacheItemIndex);
    lockedFrame.GetFrame()->Configure(this);
    if (allocateExternalFrameResourcesCallback) {
      if (!allocateExternalFrameResourcesCallback(cacheItemIndex, lockedFrame.GetFrame())) { return false; }
    }
  }
  
  return true;
}

void OpenGLXRVideo::DestroyImpl1() {
  if (framebufferInitialized) {
    gl.glDeleteFramebuffers(1, &interpolatedDeformationStateFramebuffer);
    framebufferInitialized = false;
  }
  interpolatedDeformationState.Destroy();
}

void OpenGLXRVideo::DestroyImpl1Static(int thisInt) {
  #ifdef __EMSCRIPTEN__
    OpenGLXRVideo* thisPtr = reinterpret_cast<OpenGLXRVideo*>(thisInt);
    thisPtr->DestroyImpl1();
  #else
    (void) thisInt;
  #endif
}

void OpenGLXRVideo::DestroyImpl2() {
  // Release all decoded frame locks before destroying the decoded frame cache
  decodingThread.Destroy();
  transferThread.Destroy(/*finishAllTransfers*/ false);
  #ifndef __EMSCRIPTEN__
    framesInFlightSync.clear();
  #endif
  framesLockedForRendering = vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>>();
  
  decodedFrameCache.Destroy();
}

void OpenGLXRVideo::DestroyImpl2Static(int thisInt) {
  #ifdef __EMSCRIPTEN__
    OpenGLXRVideo* thisPtr = reinterpret_cast<OpenGLXRVideo*>(thisInt);
    thisPtr->DestroyImpl2();
  #else
    (void) thisInt;
  #endif
}


void OpenGLXRVideoRenderLock::PrepareFrame(RenderState* /*renderState*/) {
  if (framesLockedForRendering.empty()) { return; }
  
  OpenGLXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  OpenGLXRVideoFrame* predecessorFrame = (framesLockedForRendering.size() == 1) ? nullptr : framesLockedForRendering.back().GetFrame();
  
  // Release locked frames that are no longer in-flight
  #ifndef __EMSCRIPTEN__
    auto& framesInFlightSync = Video()->framesInFlightSync;
    while (!framesInFlightSync.empty()) {
      GLsizei numValues;
      GLint values;
      gl.glGetSynciv(framesInFlightSync.front().renderCompleteFence, /*GL_SYNC_STATUS*/ 0x9114, /*bufSize*/ sizeof(values), &numValues, &values);
      
      if (numValues != 1) {
        LOG(ERROR) << "glGetSynciv() returned an unexpected number of values: " << numValues;
        break;
      }
      
      if (values == /*GL_UNSIGNALED*/ 0x9118) {
        // The fence is not ready yet
        break;
      }
      
      // The fence was signaled, meaning that rendering the frames has finished.
      // Thus, release the frame locks by erasing the current entry of framesInFlightSync, framesInFlightSync.front().
      // Notice that the frame locks in framesInFlightSync are an additional copy next to the one in the OpenGLXRVideoRenderLock,
      // i.e., keeping a render lock alive longer than the rendering runs will also keep the frames locked longer.
      framesInFlightSync.erase(framesInFlightSync.begin());
    }
  #endif
  
  // Run a fragment shader (used as a compute shader via rendering to texture) to interpolate the deformation state for display.
  // Start rendering to the interpolated deformation state framebuffer
  auto& interpolatedDeformationState = Video()->interpolatedDeformationState;
  
  gl.glBindFramebuffer(GL_FRAMEBUFFER, Video()->interpolatedDeformationStateFramebuffer);
  
  array<GLenum, 1> drawBuffers = {GL_COLOR_ATTACHMENT0 + 0};
  gl.glDrawBuffers(drawBuffers.size(), drawBuffers.data());
  CHECK_OPENGL_NO_ERROR();
  
  const int interpolatedDeformationStateTexelCount = 3 * frame->GetMetadata().deformationNodeCount;
  gl.glViewport(
      0, 0,
      min(interpolatedDeformationStateTexelCount, interpolatedDeformationState.Width()),
      (interpolatedDeformationStateTexelCount + interpolatedDeformationState.Width() - 1) / interpolatedDeformationState.Width());
  CHECK_OPENGL_NO_ERROR();
  
  gl.glDisable(GL_DEPTH_TEST);
  gl.glDisable(GL_CULL_FACE);
  CHECK_OPENGL_NO_ERROR();
  
  // Render
  if (frame->GetMetadata().isKeyframe) {
    const u32 coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      // Interpolate from identity to the keyframe's deformation state.
      gl.glActiveTexture(GL_TEXTURE0 + 0);
      gl.glBindTexture(GL_TEXTURE_2D, frame->GetDeformationStateTexture().Name());
      CHECK_OPENGL_NO_ERROR();
      
      Video()->CommonResources()->stateInterpolationFromIdentityShader.Use(interpolatedDeformationState.Width(), currentIntraFrameTime, /*state1TextureUnit*/ 0);
      gl.glDrawArrays(GL_TRIANGLES, /*first*/ 0, /*count*/ 3);
    }
  } else {
    // Interpolate from the previous frame's deformation state to the current frame's deformation state.
    if (predecessorFrame == nullptr) {
      LOG(ERROR) << "No predecessor frame is given, but is required for state interpolation";
      return;
    }
    if (frame->GetMetadata().deformationNodeCount != predecessorFrame->GetMetadata().deformationNodeCount) {
      LOG(ERROR) << "Inconsistency in deformationNodeCount between current and previous frame";
    }
    
    const u32 coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    
    if (coefficientCount > 0) {
      // Interpolate between two states.
      gl.glActiveTexture(GL_TEXTURE0 + 0);
      gl.glBindTexture(GL_TEXTURE_2D, predecessorFrame->GetDeformationStateTexture().Name());
      CHECK_OPENGL_NO_ERROR();
      
      gl.glActiveTexture(GL_TEXTURE0 + 1);
      gl.glBindTexture(GL_TEXTURE_2D, frame->GetDeformationStateTexture().Name());
      CHECK_OPENGL_NO_ERROR();
      
      Video()->CommonResources()->stateInterpolationShader.Use(interpolatedDeformationState.Width(), currentIntraFrameTime, /*state1TextureUnit*/ 0, /*state2TextureUnit*/ 1);
      gl.glDrawArrays(GL_TRIANGLES, /*first*/ 0, /*count*/ 3);
    }
  }
  
  // Stop rendering to the interpolated deformation state framebuffer
  gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  CHECK_OPENGL_NO_ERROR();
}

void OpenGLXRVideoRenderLock::PrepareView(int /*viewIndex*/, bool /*flipBackFaceCulling*/, bool useSurfaceNormalShading, RenderState* /*renderState*/) {
  this->useSurfaceNormalShading = useSurfaceNormalShading;
}

void OpenGLXRVideoRenderLock::RenderView(RenderState* /*renderState*/) {
  if (framesLockedForRendering.empty()) { return; }
  
  const OpenGLXRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  const OpenGLXRVideoFrame* baseKeyframe = (framesLockedForRendering.size() == 1) ? framesLockedForRendering[0].GetFrame() : framesLockedForRendering[1].GetFrame();
  
  frame->Render(
      Video()->modelView[0], Video()->modelViewProjection[0],
      Video()->modelView[1], Video()->modelViewProjection[1],
      Video()->interpolatedDeformationState.Name(),
      useSurfaceNormalShading ? &Video()->CommonResources()->xrVideoShader_surfaceNormalShading :
                                (frame->GetMetadata().hasVertexAlpha ? &Video()->CommonResources()->xrVideoShader_alphaBlending : &Video()->CommonResources()->xrVideoShader),
      baseKeyframe);
  
  // Ensure that the XRVideo frames used in rendering stay read-locked while the current rendered frame is in flight
  // (otherwise, in multi-threaded OpenGL, the decoding thread may overwrite their content while they are still being rendered, resulting in flickering).
  // TODO: When using this render path with multiple views, for better efficiency we want to run this only once after the last view has been rendered.
  #ifndef __EMSCRIPTEN__
    Video()->decodedFrameCache.Lock();
    auto readLockCopies = vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>>(framesLockedForRendering);
    Video()->decodedFrameCache.Unlock();
    
    Video()->framesInFlightSync.emplace_back(
        std::move(readLockCopies),
        gl.glFenceSync(/*GL_SYNC_GPU_COMMANDS_COMPLETE*/ 0x9117, /*flags*/ 0));
  #endif
  
  CHECK_OPENGL_NO_ERROR();
}

void OpenGLXRVideoRenderLock::SetModelViewProjection(int /*viewIndex*/, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) {
  memcpy(Video()->modelView[multiViewIndex], columnMajorModelViewData, 4 * 4 * sizeof(float));
  memcpy(Video()->modelViewProjection[multiViewIndex], columnMajorModelViewProjectionData, 4 * 4 * sizeof(float));
}

}

#endif
