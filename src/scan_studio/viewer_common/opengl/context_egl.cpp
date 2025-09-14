#if defined(__ANDROID__) || defined(__linux)
#include "scan_studio/viewer_common/opengl/context_egl.hpp"

#include <loguru.hpp>

namespace scan_studio {

static void PrintEGLError(EGLint error) {
  switch (error) {
  case EGL_SUCCESS: break;
  case EGL_NOT_INITIALIZED: LOG(ERROR) << "EGL ERROR EGL_NOT_INITIALIZED: EGL is not initialized, or could not be initialized, for the specified EGL display connection."; break;
  case EGL_BAD_ACCESS: LOG(ERROR) << "EGL ERROR EGL_BAD_ACCESS: EGL cannot access a requested resource (for example a context is bound in another thread)."; break;
  case EGL_BAD_ALLOC: LOG(ERROR) << "EGL ERROR EGL_BAD_ALLOC: EGL failed to allocate resources for the requested operation."; break;
  case EGL_BAD_ATTRIBUTE: LOG(ERROR) << "EGL ERROR EGL_BAD_ATTRIBUTE: An unrecognized attribute or attribute value was passed in the attribute list."; break;
  case EGL_BAD_CONTEXT: LOG(ERROR) << "EGL ERROR EGL_BAD_CONTEXT: An EGLContext argument does not name a valid EGL rendering context."; break;
  case EGL_BAD_CONFIG: LOG(ERROR) << "EGL ERROR EGL_BAD_CONFIG: An EGLConfig argument does not name a valid EGL frame buffer configuration."; break;
  case EGL_BAD_CURRENT_SURFACE: LOG(ERROR) << "EGL ERROR EGL_BAD_CURRENT_SURFACE: The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid."; break;
  case EGL_BAD_DISPLAY: LOG(ERROR) << "EGL ERROR EGL_BAD_DISPLAY: An EGLDisplay argument does not name a valid EGL display connection."; break;
  case EGL_BAD_SURFACE: LOG(ERROR) << "EGL ERROR EGL_BAD_SURFACE: An EGLSurface argument does not name a valid surface (window, pixel buffer or pixmap) configured for GL rendering."; break;
  case EGL_BAD_MATCH: LOG(ERROR) << "EGL ERROR EGL_BAD_MATCH: Arguments are inconsistent (for example, a valid context requires buffers not supplied by a valid surface)."; break;
  case EGL_BAD_PARAMETER: LOG(ERROR) << "EGL ERROR EGL_BAD_PARAMETER: One or more argument values are invalid."; break;
  case EGL_BAD_NATIVE_PIXMAP: LOG(ERROR) << "EGL ERROR EGL_BAD_NATIVE_PIXMAP: A NativePixmapType argument does not refer to a valid native pixmap."; break;
  case EGL_BAD_NATIVE_WINDOW: LOG(ERROR) << "EGL ERROR EGL_BAD_NATIVE_WINDOW: A NativeWindowType argument does not refer to a valid native window."; break;
  case EGL_CONTEXT_LOST: LOG(ERROR) << "EGL ERROR EGL_CONTEXT_LOST: A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering."; break;
  default: LOG(ERROR) << "An unknown EGL error has ocurred: " << error; break;
  }
}

EGLint PrintEGLError() {
  // Note: A call to eglGetError sets the error to EGL_SUCCESS, so we only need to check for one error.
  const EGLint error = eglGetError();
  PrintEGLError(error);
  return error;
}


bool EGLConfigAttributes::GetFromConfig(EGLDisplay display, EGLConfig config) {
  // Get the display's EGL version with eglInitialize().
  // (Initializing an already initialized EGL display connection has no effect besides returning the version numbers.)
  EGLint eglMajorVersion;
  EGLint eglMinorVersion;
  if (eglInitialize(display, &eglMajorVersion, &eglMinorVersion) == EGL_FALSE) {
    LOG(ERROR) << "Failed to get the EGL version using eglInitialize()";
    return false;
  }
  
  auto isEGLVersionAtLeast = [eglMajorVersion, eglMinorVersion](EGLint major, EGLint minor) {
    return eglMajorVersion > major || (eglMajorVersion == major && eglMinorVersion >= minor);
  };
  
  if (eglGetConfigAttrib(display, config, EGL_CONFIG_CAVEAT, &caveat) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_RED_SIZE, &redSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &greenSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blueSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_ALPHA_SIZE, &alphaSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depthSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_SAMPLE_BUFFERS, &sampleBuffers) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_SAMPLES, &samples) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_STENCIL_SIZE, &stencilSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  EGLint intBool;
  if (eglGetConfigAttrib(display, config, EGL_BIND_TO_TEXTURE_RGB, &intBool) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  bindToTextureRGB = (intBool == EGL_TRUE);
  if (eglGetConfigAttrib(display, config, EGL_BIND_TO_TEXTURE_RGBA, &intBool) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  bindToTextureRGBA = (intBool == EGL_TRUE);
  if (eglGetConfigAttrib(display, config, EGL_LEVEL, &level) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_WIDTH, &maxPBufferWidth) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_HEIGHT, &maxPBufferHeight) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_PIXELS, &maxPBufferPixels) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_MAX_SWAP_INTERVAL, &maxSwapInterval) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_MIN_SWAP_INTERVAL, &minSwapInterval) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_NATIVE_RENDERABLE, &nativeRenderable) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeVisualID) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_TYPE, &nativeVisualType) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &surfaceType) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_TRANSPARENT_TYPE, &transparentType) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_TRANSPARENT_RED_VALUE, &transparentRedValue) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_TRANSPARENT_GREEN_VALUE, &transparentGreenValue) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  if (eglGetConfigAttrib(display, config, EGL_TRANSPARENT_BLUE_VALUE, &transparentBlueValue) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  
  if (isEGLVersionAtLeast(1, 2)) {
    if (eglGetConfigAttrib(display, config, EGL_COLOR_BUFFER_TYPE, &colorBufferType) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
    if (eglGetConfigAttrib(display, config, EGL_ALPHA_MASK_SIZE, &alphaMaskSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
    if (eglGetConfigAttrib(display, config, EGL_LUMINANCE_SIZE, &luminanceSize) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
    if (eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE, &renderableType) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  }
  if (isEGLVersionAtLeast(1, 3)) {
    if (eglGetConfigAttrib(display, config, EGL_CONFORMANT, &conformant) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "OpenGLRenderer: eglGetConfigAttrib() failed"; return false; }
  }
  return true;
}

void EGLConfigAttributes::Print(EGLDisplay display) {
  // Get the display's EGL version with eglInitialize().
  // (Initializing an already initialized EGL display connection has no effect besides returning the version numbers.)
  EGLint eglMajorVersion;
  EGLint eglMinorVersion;
  if (eglInitialize(display, &eglMajorVersion, &eglMinorVersion) == EGL_FALSE) {
    LOG(ERROR) << "Failed to get the EGL version using eglInitialize()";
    return;
  }
  
  auto isEGLVersionAtLeast = [eglMajorVersion, eglMinorVersion](EGLint major, EGLint minor) {
    return eglMajorVersion > major || (eglMajorVersion == major && eglMinorVersion >= minor);
  };
  
  ostringstream caveatString;
  if (caveat ==  EGL_NONE) {
    caveatString << "EGL_NONE";
  } else if (caveat ==  EGL_SLOW_CONFIG) {
    caveatString << "EGL_SLOW_CONFIG";
  } else if (caveat == EGL_NON_CONFORMANT_CONFIG) {  // Should be EGL_NON_CONFORMANT according to the EGL 1.5 documentation, but that does not exist
    caveatString << "EGL_NON_CONFORMANT_CONFIG";
  } else {
    caveatString << "unknown (" << colorBufferType << ")";
  }
  LOG(1) << "caveat: " << caveatString.str();
  LOG(1) << "redSize: " << redSize;
  LOG(1) << "greenSize: " << greenSize;
  LOG(1) << "blueSize: " << blueSize;
  LOG(1) << "alphaSize: " << alphaSize;
  LOG(1) << "depthSize: " << depthSize;
  LOG(1) << "sampleBuffers: " << sampleBuffers;
  LOG(1) << "samples: " << samples;
  LOG(1) << "stencilSize: " << stencilSize;
  LOG(1) << "bindToTextureRGB: " << (bindToTextureRGB ? "true" : "false");
  LOG(1) << "bindToTextureRGBA: " << (bindToTextureRGBA ? "true" : "false");
  LOG(1) << "level: " << level;
  LOG(1) << "maxPBufferWidth: " << maxPBufferWidth;
  LOG(1) << "maxPBufferHeight: " << maxPBufferHeight;
  LOG(1) << "maxPBufferPixels: " << maxPBufferPixels;
  LOG(1) << "maxSwapInterval: " << maxSwapInterval;
  LOG(1) << "minSwapInterval: " << minSwapInterval;
  LOG(1) << "nativeRenderable: " << nativeRenderable;
  LOG(1) << "nativeVisualID: " << nativeVisualID;
  LOG(1) << "nativeVisualType: " << nativeVisualType;
  LOG(1) << "surfaceType: " << surfaceType;
  LOG(1) << "transparentType: " << transparentType;
  LOG(1) << "transparentRedValue: " << transparentRedValue;
  LOG(1) << "transparentGreenValue: " << transparentGreenValue;
  LOG(1) << "transparentBlueValue: " << transparentBlueValue;
  
  if (isEGLVersionAtLeast(1, 2)) {
    ostringstream colorBufferTypeString;
    if (colorBufferType == EGL_RGB_BUFFER) {
      colorBufferTypeString << "EGL_RGB_BUFFER";
    } else if (colorBufferType == EGL_LUMINANCE_BUFFER) {
      colorBufferTypeString << "EGL_LUMINANCE_BUFFER";
    } else {
      colorBufferTypeString << "unknown (" << colorBufferType << ")";
    }
    LOG(1) << "colorBufferType: " << colorBufferTypeString.str();
    LOG(1) << "alphaMaskSize: " << alphaMaskSize;
    LOG(1) << "luminanceSize: " << luminanceSize;
    LOG(1) << "renderableType: " << renderableType;
  }
  
  if (isEGLVersionAtLeast(1, 3)) {
    LOG(1) << "conformant: " << conformant;
  }
}


GLContextEGL::GLContextEGL() {}

GLContextEGL::~GLContextEGL() {
  Destroy();
}

bool GLContextEGL::Create(EGLConfig config, bool shareResources) {
  const EGLContext baseContext = eglGetCurrentContext();
  display = eglGetCurrentDisplay();
  
  if (baseContext == EGL_NO_CONTEXT || display == EGL_NO_DISPLAY) {
    LOG(ERROR) << "No context is current";
    return false;
  }
  
  EGLint clientVersion;
  if (eglQueryContext(display, baseContext, EGL_CONTEXT_CLIENT_VERSION, &clientVersion) == EGL_FALSE) {
    LOG(ERROR) << "Failed to query base context client version";
    clientVersion = 3;
  }
  
  EGLint attribList[] = {
      EGL_CONTEXT_MAJOR_VERSION, clientVersion,
      EGL_NONE};
  context = eglCreateContext(display, config, shareResources ? baseContext : nullptr, attribList);
  if (context == EGL_NO_CONTEXT) {
    EGLint error = eglGetError();
    
    if (shareResources && error == EGL_BAD_MATCH) {
      // One of the possible causes of EGL_BAD_MATCH when sharing resources is
      // the KHR_create_context_no_error extension being activated in the context to share resources with,
      // while not being activated in the context to be created. So, in this case, try using this extension.
      EGLint amendedAttribList[] = {
          EGL_CONTEXT_MAJOR_VERSION, clientVersion,
          EGL_CONTEXT_OPENGL_NO_ERROR_KHR, EGL_TRUE,
          EGL_NONE};
      context = eglCreateContext(display, config, baseContext, amendedAttribList);
      if (context == EGL_NO_CONTEXT) {
        error = eglGetError();
      }
    }
    
    if (context == EGL_NO_CONTEXT) {
      PrintEGLError(error);
      LOG(WARNING) << "Failed to create EGL context for OpenGL ES " << clientVersion << ".0";
      return false;
    }
  }
  
  return true;
}

void GLContextEGL::AttachToCurrent() {
  Destroy();
  
  display = eglGetCurrentDisplay();
  drawSurface = eglGetCurrentSurface(EGL_DRAW);
  readSurface = eglGetCurrentSurface(EGL_READ);
  context = eglGetCurrentContext();
}

void GLContextEGL::Detach() {
  display = EGL_NO_DISPLAY;
  drawSurface = EGL_NO_SURFACE;
  readSurface = EGL_NO_SURFACE;
  context = EGL_NO_CONTEXT;
}

void GLContextEGL::Destroy() {
  if (context) {
    if (eglDestroyContext(display, context) == EGL_FALSE) { PrintEGLError(); LOG(ERROR) << "eglDestroyContext() failed."; }
    context = EGL_NO_CONTEXT;
  }
}

bool GLContextEGL::MakeCurrent() {
  EGLDisplay displayForCall = (display == EGL_NO_DISPLAY) ? eglGetCurrentDisplay() : display;
  if (displayForCall == EGL_NO_DISPLAY) {
    displayForCall = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  }
  
  if (eglMakeCurrent(displayForCall, drawSurface, readSurface, context) == EGL_FALSE) {
    LOG(ERROR) << "eglMakeCurrent() failed";
    return false;
  }
  return true;
}

}

#endif
