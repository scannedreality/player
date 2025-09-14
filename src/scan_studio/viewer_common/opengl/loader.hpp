#pragma once

#if defined(__EMSCRIPTEN__)
  #define GL_GLES_PROTOTYPES 1
  #include <GLES/gl.h>
  #include <GLES2/gl2.h>
  #include <GLES3/gl3.h>
#elif defined(__ANDROID__)
  #include <GLES2/gl2.h>
  #include <GLES3/gl3.h>
  #ifndef APIENTRY
    #define APIENTRY GL_APIENTRY
  #endif
#elif defined(_WIN32) || defined(__APPLE__)
  #include "SDL_opengl.h"
#else
  #include "SDL_opengles2.h"
#endif

namespace scan_studio {

#if defined(SDL_VIDEO_DRIVER_UIKIT) || defined(SDL_VIDEO_DRIVER_ANDROID) || defined(__ANDROID__) || defined(SDL_VIDEO_DRIVER_PANDORA) || defined(__EMSCRIPTEN__)
#define __SDL_NOGETPROCADDR__
#endif

struct GLLoader {
  #if defined(__SDL_NOGETPROCADDR__)
    #define GLES_PROC(ret, func, params, values) inline ret func params const { return ::func values; }
    #define GLES_PROC_VOID(func, params, values) inline void func params const { ::func values; }
    #include "scan_studio/viewer_common/opengl/loader_funcs_es_2_0.hpp"
    #include "scan_studio/viewer_common/opengl/loader_funcs_es_3_0.hpp"
    #undef GLES_PROC_VOID
    #undef GLES_PROC
  #else
    #define GLES_PROC(ret, func, params, values) ret (APIENTRY *func) params = nullptr;
    #define GLES_PROC_VOID(func, params, values) void (APIENTRY *func) params = nullptr;
    #include "scan_studio/viewer_common/opengl/loader_funcs_es_2_0.hpp"
    #include "scan_studio/viewer_common/opengl/loader_funcs_es_3_0.hpp"
    #undef GLES_PROC_VOID
    #undef GLES_PROC
  #endif
  
  /// Initializes all function pointers. Returns true if successful, false otherwise.
  bool Initialize(int glesMajorVersion);
};

/// Global GLLoader instance to be able to easily access OpenGL ES 2.0 functions from everywhere
/// without having to pass the context object around everywhere.
extern GLLoader gl;

}
