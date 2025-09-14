#pragma once
#ifdef HAVE_OPENGL

#include <cstdlib>
#include <memory>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/cache.hpp"

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_frame.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/transfer_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

namespace scan_studio {
using namespace vis;

class OpenGLXRVideoCommonResources;

/// XRVideo rendering implementation using OpenGL ES 3.0 / WebGL 2.0.
///
/// Notes on implementing GPU compute in this render path:
/// OpenGL ES 3.0 does not guarantee support for rendering to float textures.
/// However, GPU-based computation can be done in the following ways:
/// * Render to a 32-bit integer texture, encoding the computed floats as integers using intBitsAsFloat() etc.
///   Then use vertex texture fetch to access the results for use in mesh deformation (vertex texture fetch is required to be supported).
///   This method has been implemented.
/// * Use vertex transform feedback to compute the interpolated deformation matrices and store them in a buffer.
///   The problem with this is that Shader Storage Buffer Objects (SSBOs) are not supported in OpenGL ES 3.0,
///   so there is no good way to read the results in a shader in case they exceed the maximum size of uniform buffers.
class OpenGLXRVideo : public XRVideoImpl<OpenGLXRVideoFrame>, public Dav1dZeroCopy {
 friend class OpenGLXRVideoRenderLock;
 public:
  OpenGLXRVideo(array<unique_ptr<GLContext>, 2>&& workerThreadContexts);
  ~OpenGLXRVideo();
  
  virtual void Destroy() override;
  
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() override;
  
  // From Dav1dZeroCopy:
  virtual int Dav1dAllocPictureCallback(Dav1dPicture* pic) override;
  virtual void Dav1dReleasePictureCallback(Dav1dPicture* pic) override;
  
  /// Accessor for the Unity plugin
  inline GLTexture& InterpolatedDeformationState() { return interpolatedDeformationState; }
  
 protected:
  virtual bool InitializeImpl() override;
  virtual bool StartLoadingThreads() override;
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) override;
  
 private:
  inline const OpenGLXRVideoCommonResources* CommonResources() const { return reinterpret_cast<OpenGLXRVideoCommonResources*>(commonResources); }
  inline OpenGLXRVideoCommonResources* CommonResources() { return reinterpret_cast<OpenGLXRVideoCommonResources*>(commonResources); }
  
  void DestroyImpl1();
  static void DestroyImpl1Static(int thisInt);
  void DestroyImpl2();
  static void DestroyImpl2Static(int thisInt);
  
  // Temporary cached data
  float modelView[2][16];
  float modelViewProjection[2][16];
  
  // Texture for holding interpolated deformation states
  GLTexture interpolatedDeformationState;
  GLuint interpolatedDeformationStateFramebuffer;
  bool framebufferInitialized = false;
  
  // For Dav1dZeroCopy:
  struct OpenGLXRVideoDav1dPicture {
    inline OpenGLXRVideoDav1dPicture(usize alignment, usize allocationSize) {
      #ifdef _WIN32
        textureDataPtr = _aligned_malloc(allocationSize, alignment);
      #else
        if (posix_memalign(&textureDataPtr, alignment, allocationSize)) {
          textureDataPtr = nullptr;
        }
      #endif
    }
    
    inline explicit OpenGLXRVideoDav1dPicture(void* textureDataPtr)
        : textureDataPtr(textureDataPtr) {}
    
    inline OpenGLXRVideoDav1dPicture(OpenGLXRVideoDav1dPicture&& other)
        : textureDataPtr(other.textureDataPtr) {
      other.textureDataPtr = nullptr;
    }
    
    OpenGLXRVideoDav1dPicture& operator= (OpenGLXRVideoDav1dPicture&& other) {
      std::swap(textureDataPtr, other.textureDataPtr);
      return *this;
    }
    
    OpenGLXRVideoDav1dPicture(const OpenGLXRVideoDav1dPicture& other) = delete;
    OpenGLXRVideoDav1dPicture& operator= (const OpenGLXRVideoDav1dPicture& other) = delete;
    
    inline ~OpenGLXRVideoDav1dPicture() {
      #ifdef _WIN32
        _aligned_free(textureDataPtr);
      #else
        free(textureDataPtr);
      #endif
    }
    
    inline void Release() {
      textureDataPtr = nullptr;
    }
    
    void* textureDataPtr;
  };
  Cache<OpenGLXRVideoDav1dPicture> dav1dPictureCache;
  
  // Synchronization to prevent resources of frames-in-flight from getting overwritten by the decoding thread.
  // (With emscripten, we use OpenGL in single-threaded mode, so this synchronization is not necessary there, as OpenGL will do it internally.)
  #ifndef __EMSCRIPTEN__
    struct FrameInFlightSync {
      inline FrameInFlightSync(
          vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>>&& framesLockedForRendering,
          GLsync renderCompleteFence)
          : framesLockedForRendering(std::move(framesLockedForRendering)),
            renderCompleteFence(renderCompleteFence) {}
      
      inline ~FrameInFlightSync() {
        gl.glDeleteSync(renderCompleteFence);
      }
      
      inline FrameInFlightSync(FrameInFlightSync&& other)
          : framesLockedForRendering(std::move(other.framesLockedForRendering)),
            renderCompleteFence(other.renderCompleteFence) {
        other.framesLockedForRendering.clear();
        other.renderCompleteFence = nullptr;
      }
      
      inline FrameInFlightSync& operator= (FrameInFlightSync&& other) {
        this->framesLockedForRendering = std::move(other.framesLockedForRendering);
        this->renderCompleteFence = other.renderCompleteFence;
        other.framesLockedForRendering.clear();
        other.renderCompleteFence = nullptr;
        return *this;
      }
      
      inline FrameInFlightSync(const FrameInFlightSync& other) = delete;
      inline FrameInFlightSync& operator= (const FrameInFlightSync& other) = delete;
      
      vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>> framesLockedForRendering;
      GLsync renderCompleteFence = nullptr;
    };
    vector<FrameInFlightSync> framesInFlightSync;
  #endif
};

class OpenGLXRVideoRenderLock : public XRVideoRenderLockImpl<OpenGLXRVideoFrame> {
 friend class OpenGLXRVideo;
 public:
  virtual void PrepareFrame(RenderState* renderState) override;
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) override;
  virtual void RenderView(RenderState* renderState) override;
  virtual inline bool SupportsLateModelViewProjectionSetting() override { return false; }
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) override;
  
  virtual inline int GetDeformationStateResourceIndex() const override { return 0; }
  
 protected:
  inline OpenGLXRVideoRenderLock(
      OpenGLXRVideo* video,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<OpenGLXRVideoFrame>>&& framesLockedForRendering)
      : XRVideoRenderLockImpl<OpenGLXRVideoFrame>(video, currentIntraFrameTime, std::move(framesLockedForRendering)) {}
  
  inline const OpenGLXRVideo* Video() const { return reinterpret_cast<OpenGLXRVideo*>(video); }
  inline OpenGLXRVideo* Video() { return reinterpret_cast<OpenGLXRVideo*>(video); }
};

}

#endif
