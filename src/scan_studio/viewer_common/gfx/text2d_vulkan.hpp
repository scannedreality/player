#pragma once
#ifdef HAVE_VULKAN

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/text2d.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/gfx/fontstash_vulkan_shader.binding.hpp"

namespace scan_studio {
using namespace vis;

struct VulkanRenderState;

class Text2DVulkan : public Text2D {
 public:
  ~Text2DVulkan();
  
  virtual bool Initialize(int viewCount, FontStash* fontStash, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) override;
  
  /// See enum FONSalign for possible values for align (e.g.: FONS_ALIGN_LEFT | FONS_ALIGN_BOTTOM)
  virtual void SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  virtual void SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  
  virtual void RenderView(int viewIndex, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) override;
  
 private:
  void RenderBatch(FONSVertex* vertices, int count, int textureIndex, int viewIndex);
  
  static constexpr u32 uniformBufferSize = 4 * 4 * sizeof(float);
  VkDeviceSize uniformBufferStride;
  VulkanBuffer uniformBuffers;
  
  VulkanDescriptorPool descriptorPool;
  vector<fontstash_vulkan_shader::DescriptorSet1> frameDescriptors;
  
  FONSText fonsText;
  
  VulkanRenderState* thisFramesRenderState = nullptr;
  
  int viewCount;
  FontStash* fontStash = nullptr;  // not owned
};

}
#endif
