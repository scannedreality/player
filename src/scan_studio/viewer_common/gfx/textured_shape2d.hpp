#pragma once

#include <memory>

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

#ifdef HAVE_VULKAN
#include <libvis/vulkan/descriptors.h>
#endif

namespace scan_studio {
using namespace vis;

struct RenderState;
class TexturedShape2DShader;

/// Represents a 2D shape with a texture and optional tinting color. Texture, tinting color and geometry can be updated per frame.
class TexturedShape2D {
 public:
  virtual inline ~TexturedShape2D() {}
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
  
  #ifdef HAVE_VULKAN
  // TODO: The way to set the texture here is currently Vulkan-specific, which it should not be.
  virtual void SetGeometry(int vertexCount, const void* vertices, int indexCount, const u16* indices, const VkDescriptorImageInfo* texture, RenderState* renderState) = 0;
  #endif
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, TexturedShape2DShader* shader, RenderState* renderState) = 0;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) = 0;
  
  static TexturedShape2D* Create(int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState);
  static bool Create(unique_ptr<TexturedShape2D>* ptr, int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState);
  
  inline int MaxVertices() const { return maxVertices; }
  inline int MaxIndices() const { return maxIndices; }
  
 protected:
  int maxVertices;
  int maxIndices;
};

}
