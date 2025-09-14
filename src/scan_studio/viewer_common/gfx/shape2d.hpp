#pragma once

#include <memory>

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

struct RenderState;
class Shape2DShader;

/// Represents a homogeneously colored 2D shape. Color and geometry can be updated per frame.
class Shape2D {
 public:
  virtual inline ~Shape2D() {}
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
  
  virtual void SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* renderState) = 0;
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, Shape2DShader* shader, RenderState* renderState) = 0;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) = 0;
  
  static Shape2D* Create(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState);
  static bool Create(unique_ptr<Shape2D>* ptr, int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState);
  
  inline int MaxVertices() const { return maxVertices; }
  inline int MaxIndices() const { return maxIndices; }
  
 protected:
  int maxVertices;
  int maxIndices;
};

}
