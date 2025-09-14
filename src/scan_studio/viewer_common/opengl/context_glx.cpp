#if defined(__linux) && !defined(__ANDROID__)
#include "scan_studio/viewer_common/opengl/context_glx.hpp"

#include <loguru.hpp>

namespace scan_studio {

GLContextGLX::~GLContextGLX() {
  Destroy();
}

static int XErrorHandler(Display* dsp, XErrorEvent* error) {
  constexpr int kBufferSize = 512;
  char error_string[kBufferSize];
  XGetErrorText(dsp, error->error_code, error_string, kBufferSize);

  LOG(ERROR) << "X Error:\n" << error_string;
  return 0;
}

bool GLContextGLX::InitializeWindowless(GLContextGLX* sharing_context) {
  auto oldErrorHandler = XSetErrorHandler(XErrorHandler);
  
  display = XOpenDisplay(nullptr);
  if (!display) { LOG(ERROR) << "Cannot connect to X server."; Destroy(); return false; }
  drawable = DefaultRootWindow(display);
  
  GLint attributes[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, None};
  XVisualInfo* visual = glXChooseVisual(display, 0, attributes);
  if (!visual) { LOG(ERROR) << "No appropriate visual found."; Destroy(); return false; }
  
  context = glXCreateContext(display, visual, sharing_context ? sharing_context->context : nullptr, GL_TRUE);
  XFree(visual);
  if (!context) { LOG(ERROR) << "Cannot create GLX context."; Destroy(); return false; }
  
  XSetErrorHandler(oldErrorHandler);
  return true;
}

void GLContextGLX::AttachToCurrent() {
  Destroy();
  
  display = glXGetCurrentDisplay();
  drawable = glXGetCurrentDrawable();
  context = glXGetCurrentContext();
}

void GLContextGLX::Detach() {
  display = nullptr;
  drawable = None;
  context = nullptr;
}

void GLContextGLX::Destroy() {
  if (context) {
    glXDestroyContext(display, context);
    context = nullptr;
  }
  
  if (display) {
    XCloseDisplay(display);
    display = nullptr;
  }
  
  drawable = None;
}

bool GLContextGLX::MakeCurrent() {
  auto oldErrorHandler = XSetErrorHandler(XErrorHandler);
  
  const bool result = glXMakeCurrent(display, drawable, context) == GL_TRUE;
  
  XSetErrorHandler(oldErrorHandler);
  
  return result;
}

}

#endif
