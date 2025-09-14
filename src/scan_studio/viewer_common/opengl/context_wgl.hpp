#pragma once
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#define NOCOMM
#include <Windows.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/context.hpp"

namespace scan_studio {
using namespace vis;

class GLContextWGL : public GLContext {
 public:
  virtual ~GLContextWGL() override;
  
  /// NOTE: This function currently assumes that sharing_context is used, and that OpenGL context is already current in the calling thread
  ///       (since querying WGL extensions requires a context, and since the function queries the OpenGL version and profile of the current context, as well as its DC). See:
  ///       https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions#Windows_2
  bool InitializeWindowless(GLContextWGL* sharing_context = nullptr);
  
  inline bool IsValid() const { return context != nullptr; }
  
  virtual void AttachToCurrent() override;
  virtual void Detach() override;
  
  /// From the WGL documentation:
  /// "It is an error to delete an OpenGL rendering context that is the current context of another thread.
  ///  However, if a rendering context is the calling thread's current context, the wglDeleteContext
  ///  function changes the rendering context to being not current before deleting it."
  virtual void Destroy() override;
  
  virtual bool MakeCurrent() override;
  
 private:
  HDC dc = nullptr;
  HGLRC context = nullptr;
};

}

#endif
