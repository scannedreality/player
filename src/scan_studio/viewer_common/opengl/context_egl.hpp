#pragma once
#if defined(__ANDROID__) || defined(__linux)

#include <EGL/egl.h>
#include <EGL/eglext.h>
#undef Success  // This somehow seems to get defined by the include above and causes trouble in Eigen

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/context.hpp"

namespace scan_studio {
using namespace vis;

/// Checks for whether eglGetError() returns that an EGL error occurred,
/// and if so, prints the error message to LOG(ERROR).
/// Returns the EGL error code that was printed.
EGLint PrintEGLError();

struct EGLConfigAttributes {
  bool GetFromConfig(EGLDisplay display, EGLConfig config);
  void Print(EGLDisplay display);
  
  EGLint caveat = EGL_NONE;
  
  EGLint redSize = 0;
  EGLint greenSize = 0;
  EGLint blueSize = 0;
  EGLint alphaSize = 0;
  
  EGLint depthSize = 0;
  
  EGLint sampleBuffers = 0;
  EGLint samples = 0;
  
  EGLint stencilSize = 0;
  
  bool bindToTextureRGB;
  bool bindToTextureRGBA;
  
  EGLint level = -1;
  
  EGLint maxPBufferWidth = -1;
  EGLint maxPBufferHeight = -1;
  EGLint maxPBufferPixels = -1;
  
  EGLint maxSwapInterval = -1;
  EGLint minSwapInterval = -1;
  
  EGLint nativeRenderable = -1;
  EGLint nativeVisualID = -1;
  EGLint nativeVisualType = -1;
  
  EGLint surfaceType = -1;
  
  EGLint transparentType = -1;
  EGLint transparentRedValue = -1;
  EGLint transparentGreenValue = -1;
  EGLint transparentBlueValue = -1;
  
  // The attributes below can only be queried if IsEGLVersionAtLeast(1, 2) returns true.
  EGLint colorBufferType = EGL_RGB_BUFFER;
  EGLint alphaMaskSize = 0;
  EGLint luminanceSize = 0;
  EGLint renderableType = 0;
  
  // The attributes below can only be queried if IsEGLVersionAtLeast(1, 3) returns true.
  EGLint conformant = 0;
};

class GLContextEGL : public GLContext {
 public:
  /// Constructor, does nothing.
  GLContextEGL();
  
  /// Destructor deleting the context.
  ~GLContextEGL();
  
  /// Creates a new windowless context with the given EGLConfig, using the thread's current context's display,
  /// optionally sharing resources with the thread's current context. Returns true on success, false on failure.
  bool Create(EGLConfig config, bool shareResources);
  
  virtual void AttachToCurrent() override;
  
  virtual void Detach() override;
  
  /// If the EGL rendering context is not current to any thread, eglDestroyContext() destroys it immediately.
  /// Otherwise, the context is destroyed when it becomes not current to any thread.
  virtual void Destroy() override;
  
  virtual bool MakeCurrent() override;
  
 private:
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface drawSurface = EGL_NO_SURFACE;
  EGLSurface readSurface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
};

}

#endif
