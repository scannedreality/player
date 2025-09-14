#include "scan_studio/viewer_common/platform/render_window_sdl_opengl.hpp"

#include <loguru.hpp>

namespace scan_studio {

RenderWindowSDLOpenGL::RenderWindowSDLOpenGL() {}

RenderWindowSDLOpenGL::~RenderWindowSDLOpenGL() {}

void RenderWindowSDLOpenGL::SetRequestedOpenGLVersion(int majorVersion, int minorVersion, bool useOpenGLES) {
  requestedMajorVersion = majorVersion;
  requestedMinorVersion = minorVersion;
  openGLESRequested = useOpenGLES;
}

void RenderWindowSDLOpenGL::Deinitialize() {
  callbacks_->DeinitializeSurfaceDependent();
  callbacks_->Deinitialize();
  
  if (window_) {
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

bool RenderWindowSDLOpenGL::InitializeImpl(const char* title, int width, int height, WindowState windowState) {
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  #ifndef __EMSCRIPTEN__  // SDL window creation fails with emscripten if SDL_GL_FRAMEBUFFER_SRGB_CAPABLE is enabled
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
  #endif
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, requestedMajorVersion);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, requestedMinorVersion);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, openGLESRequested ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
  
  // Setup window
  window_ = SDL_CreateWindow(
      title,
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height,
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
          | ((windowState == RenderWindowSDL::WindowState::Maximized) ? SDL_WINDOW_MAXIMIZED : 0)
          | ((windowState == RenderWindowSDL::WindowState::Fullscreen) ? (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS) : 0)));
  if (!window_) {
    LOG(ERROR) << "Failed to initialize SDL window";
    return false;
  }
  
  context_ = SDL_GL_CreateContext(window_);
  
  if (!callbacks_->Initialize()) { return false; }
  if (!callbacks_->InitializeSurfaceDependent()) { return false; }
  
  // Let the callback object know about the initial window size.
  // Note that the SDL function to get the drawable size appears to be OpenGL-specific.
  SDL_GL_GetDrawableSize(window_, &window_area_width, &window_area_height);
  callbacks_->Resize(window_area_width, window_area_height);
  
  return true;
}

void RenderWindowSDLOpenGL::GetDrawableSize(int* width, int* height) {
  SDL_GL_GetDrawableSize(window_, width, height);
}

void RenderWindowSDLOpenGL::Render() {
  callbacks_->Render(/*unused: image_index*/ -1);
  
  SDL_GL_SwapWindow(window_);
}

}
