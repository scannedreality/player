#pragma once
#ifdef HAVE_OPENGL

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/shape2d.hpp"

#include "scan_studio/viewer_common/opengl/buffer.hpp"

namespace scan_studio {
using namespace vis;

struct RenderState;

class Shape2DOpenGL : public Shape2D {
 public:
  ~Shape2DOpenGL();
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* renderState) override;
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) override;
  
 private:
  GLBuffer vertexBuffer;
  GLBuffer indexBuffer;
  
  float modelViewProjection[16];
  
  int indexCount;
};

}

#endif
