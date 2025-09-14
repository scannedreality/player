#pragma once
#ifdef HAVE_VULKAN

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/shape2d.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/gfx/shape2d_vulkan_shader.binding.hpp"

namespace scan_studio {
using namespace vis;

struct RenderState;

class Shape2DVulkan : public Shape2D {
 public:
  ~Shape2DVulkan();
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* renderState) override;
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float *columnMajorModelViewProjectionData) override;
  
 private:
  VulkanBuffer vertexBuffer;
  VulkanBuffer indexBuffer;
  
  VulkanBuffer vertexBufferStaging;
  VulkanBuffer indexBufferStaging;
  
  static constexpr u32 uniformBufferSize = 4 * 4 * sizeof(float);
  VkDeviceSize uniformBufferStride;
  VulkanBuffer uniformBuffers;
  
  VulkanDescriptorPool descriptorPool;
  vector<shape2d_vulkan_shader::DescriptorSet0> descriptorSets;
  
  int currentFrameInFlightIndex = 0;
  int viewCount;
  int indexCount;
};

}
#endif
