#pragma once

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
  #include <SDL_opengl_glext.h>
#else
  #include <GLES2/gl2.h>
  #define HAVE_OPENGL_ES
#endif

namespace scan_studio {

struct GLES2Extensions {
  // Note: We don't use this OpenGL ES 2 extension anymore. Improved buffer mapping functionality is integrated in OpenGL ES 3 without the need for an extension.
  // #if defined(HAVE_OPENGL_ES)
  //   typedef void* (GL_APIENTRYP GL_MapBufferOES_Func) (GLenum target, GLenum access);
  //   typedef GLboolean (GL_APIENTRYP GL_UnmapBufferOES_Func) (GLenum target);
  //   
  //   GL_MapBufferOES_Func mapBufferOES = nullptr;
  //   GL_UnmapBufferOES_Func unmapBufferOES = nullptr;
  // #endif
  
  /// Initializes all function pointers. Returns true if successful, false otherwise.
  bool Initialize();
};

/// Global GLES2Extensions instance to be able to easily access OpenGL ES 2.0 functions from everywhere
/// without having to pass the context object around everywhere.
extern GLES2Extensions glExt;

}
