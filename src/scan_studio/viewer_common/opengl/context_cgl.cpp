#if defined(__APPLE__)
#include "scan_studio/viewer_common/opengl/context_cgl.hpp"

#include <loguru.hpp>

namespace scan_studio {

/* We still support OpenGL as long as Apple offers it, deprecated or not, so disable deprecation warnings about it. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static void PrintCGLError(CGLError error) {
  switch (error) {
  case kCGLNoError: break;
  case kCGLBadAttribute: LOG(ERROR) << "kCGLBadAttribute: invalid pixel format attribute"; break;
  case kCGLBadProperty: LOG(ERROR) << "kCGLBadProperty: invalid renderer property"; break;
  case kCGLBadPixelFormat: LOG(ERROR) << "kCGLBadPixelFormat: invalid pixel format"; break;
  case kCGLBadRendererInfo: LOG(ERROR) << "kCGLBadRendererInfo: invalid renderer info"; break;
  case kCGLBadContext: LOG(ERROR) << "kCGLBadContext: invalid context"; break;
  case kCGLBadDrawable: LOG(ERROR) << "kCGLBadDrawable: invalid drawable"; break;
  case kCGLBadDisplay: LOG(ERROR) << "kCGLBadDisplay: invalid graphics device"; break;
  case kCGLBadState: LOG(ERROR) << "kCGLBadState: invalid context state"; break;
  case kCGLBadValue: LOG(ERROR) << "kCGLBadValue: invalid numerical value"; break;
  case kCGLBadMatch: LOG(ERROR) << "kCGLBadMatch: invalid share context"; break;
  case kCGLBadEnumeration: LOG(ERROR) << "kCGLBadEnumeration: invalid enumerant"; break;
  case kCGLBadOffScreen: LOG(ERROR) << "kCGLBadOffScreen: invalid offscreen drawable"; break;
  case kCGLBadFullScreen: LOG(ERROR) << "kCGLBadFullScreen: invalid fullscreen drawable"; break;
  case kCGLBadWindow: LOG(ERROR) << "kCGLBadWindow: invalid window"; break;
  case kCGLBadAddress: LOG(ERROR) << "kCGLBadAddress: invalid pointer"; break;
  case kCGLBadCodeModule: LOG(ERROR) << "kCGLBadCodeModule: invalid code module"; break;
  case kCGLBadAlloc: LOG(ERROR) << "kCGLBadAlloc: invalid memory allocation"; break;
  case kCGLBadConnection: LOG(ERROR) << "kCGLBadConnection: invalid CoreGraphics connection"; break;
  default: LOG(ERROR) << "An unknown CGL error has ocurred: " << error; break;
  }
}

GLContextCGL::~GLContextCGL() {
  Destroy();
}

bool GLContextCGL::InitializeWindowless(GLContextCGL* sharing_context) {
  CGLPixelFormatAttribute attributes[4] = {
      kCGLPFAAccelerated,   // no software rendering
      kCGLPFAOpenGLProfile, // core profile with the version stated below
      (CGLPixelFormatAttribute) kCGLOGLPVersion_GL4_Core,
      (CGLPixelFormatAttribute) 0
  };
  
  CGLPixelFormatObj pixelFormat;
  GLint virtualScreenCount;
  CGLError errorCode = CGLChoosePixelFormat(attributes, &pixelFormat, &virtualScreenCount);
  if (errorCode != kCGLNoError) {
    LOG(ERROR) << "CGLChoosePixelFormat() failed with error:";
    PrintCGLError(errorCode);
    return false;
  }
  
  errorCode = CGLCreateContext(pixelFormat, sharing_context ? sharing_context->context : nullptr, &context);
  CGLDestroyPixelFormat(pixelFormat);
  if (errorCode != kCGLNoError) {
    LOG(ERROR) << "CGLCreateContext() failed with error:";
    PrintCGLError(errorCode);
    return false;
  }
  
  return true;
}

void GLContextCGL::AttachToCurrent() {
  Destroy();
  
  context = CGLGetCurrentContext();
}

void GLContextCGL::Detach() {
  context = nullptr;
}

void GLContextCGL::Destroy() {
  if (context) {
    CGLDestroyContext(context);
    context = nullptr;
  }
}

bool GLContextCGL::MakeCurrent() {
  const CGLError errorCode = CGLSetCurrentContext(context);
  if (errorCode != kCGLNoError) {
    LOG(ERROR) << "CGLSetCurrentContext() failed with error:";
    PrintCGLError(errorCode);
    return false;
  }
  return true;
}

/* We still support OpenGL as long as Apple offers it, deprecated or not, so disable deprecation warnings about it. */
#ifdef __clang__
#pragma clang diagnostic pop
#endif

}

#endif
