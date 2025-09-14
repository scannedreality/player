#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_common_resources.hpp"
#ifdef HAVE_OPENGL

#ifdef __EMSCRIPTEN__
#include <emscripten/threading.h>
#endif

namespace scan_studio {

OpenGLXRVideoCommonResources::~OpenGLXRVideoCommonResources() {
  Destroy();
}

bool OpenGLXRVideoCommonResources::Initialize(bool outputLinearColors, bool use_GL_OVR_multiview2) {
  // Initialize the XRVideo rendering shader
  if (!xrVideoShader.Initialize(outputLinearColors, /*useAlphaBlending*/ false, /*useSurfaceNormalShading*/ false, use_GL_OVR_multiview2)) { return false; }
  if (!xrVideoShader_alphaBlending.Initialize(outputLinearColors, /*useAlphaBlending*/ true, /*useSurfaceNormalShading*/ false, use_GL_OVR_multiview2)) { return false; }
  if (!xrVideoShader_surfaceNormalShading.Initialize(outputLinearColors, /*useAlphaBlending*/ false, /*useSurfaceNormalShading*/ true, use_GL_OVR_multiview2)) { return false; }
  
  // Initialize the state interpolation shaders
  if (!stateInterpolationFromIdentityShader.Initialize(/*interpolateFromIdentity*/ true)) { return false; }
  if (!stateInterpolationShader.Initialize(/*interpolateFromIdentity*/ false)) { return false; }
  
  return true;
}

void OpenGLXRVideoCommonResources::Destroy() {
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &OpenGLXRVideoCommonResources::DestroyImplStatic, reinterpret_cast<int>(this));
  #else
    DestroyImpl();
  #endif
}

void OpenGLXRVideoCommonResources::DestroyImpl() {
  stateInterpolationShader.Destroy();
  stateInterpolationFromIdentityShader.Destroy();
  xrVideoShader.Destroy();
  xrVideoShader_alphaBlending.Destroy();
  xrVideoShader_surfaceNormalShading.Destroy();
}

void OpenGLXRVideoCommonResources::DestroyImplStatic(int thisInt) {
  #ifdef __EMSCRIPTEN__
    OpenGLXRVideoCommonResources* thisPtr = reinterpret_cast<OpenGLXRVideoCommonResources*>(thisInt);
    thisPtr->DestroyImpl();
  #else
    (void) thisInt;
  #endif
}

}

#endif
