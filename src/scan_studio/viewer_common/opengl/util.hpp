#pragma once

#include <string>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <Eigen/Core>

#include <loguru.hpp>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

Eigen::Matrix4f PerspectiveMatrixOpenGL(float verticalFov, float aspectRatio, float zNear, float zFar);

string GetGLErrorName(GLenum error_code);

string GetGLErrorDescription(GLenum error_code);

/// Macro checking for OpenGL errors using glGetError().
/// This uses a macro such that LOG(ERROR) picks up the correct file and line number.
///
/// Attention: glGetError() may be extremely slow depending on the platform (e.g., when running with emscripten in Chrome).
///            Thus, it is probably best to omit this from release builds entirely by default.
#ifdef DEB_INFO_BUILD
#define CHECK_OPENGL_NO_ERROR() \
  do { \
    GLenum error = gl.glGetError(); \
    if (error == GL_NO_ERROR) {  \
      break; \
    } \
    LOG(ERROR) << "OpenGL Error: " << GetGLErrorName(error) << " (" << error << "), description:" << endl << GetGLErrorDescription(error); \
  } while (true)
#else
#define CHECK_OPENGL_NO_ERROR() do {} while (false)
#endif

}
