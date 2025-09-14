#pragma once
#if defined(__APPLE__)

#include <OpenGL/OpenGL.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/context.hpp"

namespace scan_studio {
using namespace vis;

/// For a reference on CGL, see:
/// https://web.archive.org/web/20140812200349/https://developer.apple.com/library/mac/documentation/GraphicsImaging/Reference/CGL_OpenGL/Reference/reference.html#//apple_ref/c/func/CGLGetCurrentContext
class GLContextCGL : public GLContext {
 public:
  virtual ~GLContextCGL() override;
  
  bool InitializeWindowless(GLContextCGL* sharing_context = nullptr);
  
  inline bool IsValid() const { return context != nullptr; }
  
  virtual void AttachToCurrent() override;
  virtual void Detach() override;
  virtual void Destroy() override;
  virtual bool MakeCurrent() override;
  
 private:
  CGLContextObj context = nullptr;
};

}

#endif
