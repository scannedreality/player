#pragma once
#ifdef HAVE_VULKAN

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/textured_shape2d.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/gfx/textured_shape2d_vulkan_shader.binding.hpp"

namespace scan_studio {
using namespace vis;

struct RenderState;

class TexturedShape2DVulkan : public TexturedShape2D {
 public:
  ~TexturedShape2DVulkan();
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void SetGeometry(int vertexCount, const void* vertices, int indexCount, const u16* indices, const VkDescriptorImageInfo* texture, RenderState* renderState) override;
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, TexturedShape2DShader* shader, RenderState* renderState) override;
  
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
  vector<textured_shape2d_vulkan_shader::DescriptorSet0> perViewDescriptorSets;
  vector<textured_shape2d_vulkan_shader::DescriptorSet1> perFrameDescriptorSets;
  
  int currentFrameInFlightIndex = 0;
  int viewCount;
  int indexCount;
};

}
#endif
