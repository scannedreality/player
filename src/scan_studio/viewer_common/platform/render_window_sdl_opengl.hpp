#pragma once

#include <memory>

#include <SDL.h>
#ifdef __EMSCRIPTEN__
  #include <SDL_opengles2.h>
#else
  #include <SDL_opengl.h>
#endif

#include <libvis/util/window_callbacks.h>

#include "scan_studio/viewer_common/platform/render_window_sdl.hpp"

struct SDL_Window;

namespace scan_studio {
using namespace vis;

class RenderWindowSDLOpenGL : public scan_studio::RenderWindowSDL {
 public:
  RenderWindowSDLOpenGL();
  ~RenderWindowSDLOpenGL();
  
  void SetRequestedOpenGLVersion(int majorVersion, int minorVersion, bool useOpenGLES);
  
  virtual void Deinitialize() override;
  
 protected:
  virtual bool InitializeImpl(
      const char* title,
      int width,
      int height,
      WindowState windowState) override;
  virtual void GetDrawableSize(int *width, int *height) override;
  virtual void Render() override;
  
 private:
  SDL_GLContext context_;
  
  int requestedMajorVersion = 2;
  int requestedMinorVersion = 0;
  bool openGLESRequested = true;
};

}
