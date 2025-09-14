#pragma once
#ifdef HAVE_OPENGL

#include <memory>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/buffer.hpp"
#include "scan_studio/viewer_common/opengl/shader.hpp"

namespace scan_studio {
using namespace vis;

class OpenGLXRVideoShader {
 public:
  ~OpenGLXRVideoShader();
  
  bool Initialize(bool outputLinearColors, bool useAlphaBlending, bool useSurfaceNormalShading, bool use_GL_OVR_multiview2);
  void Destroy();
  
  void Use(
      GLuint vertexBuffer, GLuint vertexAlphaBuffer, u32 textureWidth, u32 textureHeight,
      const float* modelViewDataColumnMajor, const float* modelViewProjectionDataColumnMajor,
      const float* modelViewDataColumnMajor2, const float* modelViewProjectionDataColumnMajor2,
      float bboxMinX, float bboxMinY, float bboxMinZ, float vertexFactorX, float vertexFactorY, float vertexFactorZ);
  
  void DoneUsing();
  
 private:
  shared_ptr<ShaderProgram> program;
  
  vector<u8> ubo;
  GLBuffer uboBuffer;
  
  GLint inPosAttrib;
  GLint inTexcoordAttrib;
  GLint inNodeIndicesAttrib;
  GLint inNodeWeightsAttrib;
  GLint inVertexAlphaAttrib;
  
  GLuint uniformBlockIndex;
  GLsizei uniformBlockSize;
  
  GLint deformationStateLocation;
  
  #ifdef __EMSCRIPTEN__
    GLint textureYUVLocation;
  #else
    GLint textureLumaLocation;
    GLint textureChromaULocation;
    GLint textureChromaVLocation;
  #endif
  GLint textureSizeLocation;
  
  bool useAlphaBlending;
  bool useSurfaceNormalShading;
  bool use_GL_OVR_multiview2;
};

class OpenGLXRVideoStateInterpolationShader {
 public:
  ~OpenGLXRVideoStateInterpolationShader();
  
  bool Initialize(bool interpolateFromIdentity);
  void Destroy();
  
  void Use(int viewportWidth, float factor, int state1TextureUnit, int state2TextureUnit = 0);
  
 private:
  shared_ptr<ShaderProgram> program;
  
  GLint viewportWidthLocation;
  GLint factorLocation;
  
  GLint state1Location;
  GLint state2Location;
  
  bool interpolateFromIdentity;
};

}

#endif
