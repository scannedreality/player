#include "scan_studio/viewer_common/opengl/extensions.hpp"

#ifndef __ANDROID__
  #include "SDL_error.h"
  #include "SDL_video.h"
#endif

namespace scan_studio {

bool GLES2Extensions::Initialize() {
  // #if defined(HAVE_OPENGL_ES) && !defined(__ANDROID__)  // TODO: Implement for Android as well (where we don't use SDL)
  //   // Query support for GL_OES_mapbuffer
  //   if (SDL_GL_ExtensionSupported("GL_OES_mapbuffer")) {
  //     // Try to get the function pointers for this extension, and if any of them is NULL,
  //     // treat the extension as not supported.
  //     mapBufferOES = (GL_MapBufferOES_Func) SDL_GL_GetProcAddress("glMapBufferOES");
  //     unmapBufferOES = (GL_UnmapBufferOES_Func) SDL_GL_GetProcAddress("glUnmapBufferOES");
  //     if (!mapBufferOES || !unmapBufferOES) {
  //       mapBufferOES = nullptr;
  //       unmapBufferOES = nullptr;
  //     }
  //   }
  // #endif
  
  return true;
}

GLES2Extensions glExt;

}
