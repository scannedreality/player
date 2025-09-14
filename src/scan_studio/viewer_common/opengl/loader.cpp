#include "scan_studio/viewer_common/opengl/loader.hpp"

#ifdef __ANDROID__
  #define SDL_VIDEO_DRIVER_ANDROID 1
#else
  #include "SDL_error.h"
  #include "SDL_video.h"
#endif

#include <loguru.hpp>

namespace scan_studio {

#if defined(_WIN32)
// Note: We don't use SDL on Windows since calling SDL_Init() and SDL_GL_LoadLibrary() in the Unity plugin
//       made Unity's OpenGL context non-current.

// Function taken from:
// https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions
static void* GetWinGLFuncAddress(const char* name) {
  void* p = (void*)wglGetProcAddress(name);
  if (p == 0 ||
     (p == (void*)0x1) || (p == (void*)0x2) || (p == (void*)0x3) ||
     (p == (void*)-1)) {
    HMODULE module = LoadLibraryA("opengl32.dll");
    p = (void*)GetProcAddress(module, name);
  }
  return p;
}
#endif

bool GLLoader::Initialize(int glesMajorVersion) {
  if (glesMajorVersion >= 2) {
    #if defined __SDL_NOGETPROCADDR__
      #define GLES_PROC(ret, func, params, values) ;
    #elif defined(_WIN32)
      #define GLES_PROC(ret, func, params, values) \
          do { \
            this->func = (ret (APIENTRY *) params) GetWinGLFuncAddress(#func); \
            if (!this->func) { \
              LOG(ERROR) << "Failed to load OpenGL function: " << #func; \
              return false; \
            } \
          } while (false);
    #else
      #define GLES_PROC(ret, func, params, values) \
          do { \
            this->func = (ret (APIENTRY *) params) SDL_GL_GetProcAddress(#func); \
            if (!this->func) { \
              LOG(ERROR) << "Failed to load OpenGL function: " << #func; \
              return false; \
            } \
          } while (false);
    #endif /* __SDL_NOGETPROCADDR__ */
    #define GLES_PROC_VOID(func, params, values) GLES_PROC(void, func, params, values)
    #include "scan_studio/viewer_common/opengl/loader_funcs_es_2_0.hpp"
    if (glesMajorVersion >= 3) {
      #include "scan_studio/viewer_common/opengl/loader_funcs_es_3_0.hpp"
    }
    #undef GLES_PROC_VOID
    #undef GLES_PROC
  }
  
  return true;
}

GLLoader gl;

}
