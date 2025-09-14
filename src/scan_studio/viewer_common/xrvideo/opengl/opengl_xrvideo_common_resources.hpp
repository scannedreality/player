#pragma once
#ifdef HAVE_OPENGL

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_shader.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_common_resources.hpp"

namespace scan_studio {
using namespace vis;

/// Initializes and stores common resources that are required for rendering XRVideos,
/// for example, compiled shaders.
class OpenGLXRVideoCommonResources : public XRVideoCommonResources {
 friend class OpenGLXRVideo;
 friend class OpenGLXRVideoFrame;
 friend class OpenGLXRVideoRenderLock;
 public:
  /// Default constructor, does not initialize the object.
  OpenGLXRVideoCommonResources() = default;
  
  OpenGLXRVideoCommonResources(const OpenGLXRVideoCommonResources& other) = delete;
  OpenGLXRVideoCommonResources& operator= (const OpenGLXRVideoCommonResources& other) = delete;
  
  ~OpenGLXRVideoCommonResources();
  
  bool Initialize(bool outputLinearColors, bool use_GL_OVR_multiview2);
  void Destroy();
  
 private:
  void DestroyImpl();
  static void DestroyImplStatic(int thisInt);
  
  // TODO: Instead of always having variants for useSurfaceNormalShading on/off here, add a mechanism to either
  //       specify up-front which variants will actually be needed, or to add them on demand.
  //       Note that for the other options that currently exist (outputLinearColors, use_GL_OVR_multiview2),
  //       it is unlikely that both settings will be needed during the same program invocation,
  //       which differs from the case of useSurfaceNormalShading.
  OpenGLXRVideoShader xrVideoShader;
  OpenGLXRVideoShader xrVideoShader_alphaBlending;
  OpenGLXRVideoShader xrVideoShader_surfaceNormalShading;
  
  OpenGLXRVideoStateInterpolationShader stateInterpolationFromIdentityShader;
  OpenGLXRVideoStateInterpolationShader stateInterpolationShader;
};

}

#endif
