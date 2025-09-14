#ifndef __ANDROID__
#include "scan_studio/viewer_common/opengl/context_sdl.hpp"

#include <loguru.hpp>

namespace scan_studio {

GLContextSDL::~GLContextSDL() {
  Destroy();
}

void GLContextSDL::AttachToCurrent() {
  Destroy();
  
  window = SDL_GL_GetCurrentWindow();
  context = SDL_GL_GetCurrentContext();
}

void GLContextSDL::Detach() {
  window = nullptr;
  context = nullptr;
}

void GLContextSDL::Destroy() {
  if (context) {
    SDL_GL_DeleteContext(context);
    context = nullptr;
  }
}

bool GLContextSDL::MakeCurrent() {
  const int result = SDL_GL_MakeCurrent(window, context);
  if (result == 0) {
    return true;
  }
  
  LOG(ERROR) << "Failed to make an OpenGL context current, error code: " << result << ", message: " << SDL_GetError();
  return false;
}

}

#endif
