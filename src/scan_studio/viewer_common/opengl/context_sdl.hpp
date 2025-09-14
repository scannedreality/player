#pragma once
#ifndef __ANDROID__

#include <SDL.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/context.hpp"

namespace scan_studio {
using namespace vis;

class GLContextSDL : public GLContext {
 public:
  inline GLContextSDL() {}
  
  /// Constructor taking ownership of the given context.
  inline GLContextSDL(SDL_Window* window, SDL_GLContext context)
      : window(window),
        context(context) {}
  
  /// Destructor deleting the context.
  ~GLContextSDL();
  
  // TODO: It would be nice to have a Create() function here as for GLContextEGL,
  //       however, it seems that SDL can only create contexts linked to SDL_Window instances,
  //       and resource sharing works by creating multiple contexts for the same SDL_Window,
  //       which makes this not suitable for use cases where we have to share resources with
  //       pre-existing contexts that don't have an SDL_Window associated
  //       (for example, a context created by Unity, for our Unity plugin).
  
  virtual void AttachToCurrent() override;
  
  virtual void Detach() override;
  
  virtual void Destroy() override;
  
  /// Attention: According to the following post, SDL_GL_MakeCurrent() is not thread-safe on all platforms:
  ///            https://stackoverflow.com/questions/64484835/how-to-setup-one-shared-opengl-contexts-per-thread-with-sdl2
  virtual bool MakeCurrent() override;
  
 private:
  SDL_Window* window = nullptr;
  SDL_GLContext context = nullptr;
};

}

#endif
