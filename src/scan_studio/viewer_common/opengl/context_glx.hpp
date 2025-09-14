#pragma once
#if defined(__linux) && !defined(__ANDROID__)

#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/context.hpp"

namespace scan_studio {
using namespace vis;

// Note: GLX reference pages are here:
//       https://registry.khronos.org/OpenGL-Refpages/gl2.1/
//
// TODO: There is also a GLX context class in libvis, which this one here is based on
//       (this new one here has some minor improvements). Merge the two classes.
class GLContextGLX : public GLContext {
 public:
  virtual ~GLContextGLX() override;
  
  bool InitializeWindowless(GLContextGLX* sharing_context = nullptr);
  
  virtual void AttachToCurrent() override;
  virtual void Detach() override;
  virtual void Destroy() override;
  virtual bool MakeCurrent() override;
  
 private:
  Display* display = nullptr;
  GLXDrawable drawable = None;
  GLXContext context = nullptr;
};

}

#endif
