#pragma once
#ifdef HAVE_VULKAN

#include <vector>

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/command_buffer.h>
#include <libvis/vulkan/command_pool.h>
#include <libvis/vulkan/device.h>
#include <libvis/vulkan/fence.h>
#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/texture.h>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_frame.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader.binding.hpp"

namespace scan_studio {
using namespace vis;

class TextureFramePromise;
class VulkanXRVideo;

/// Represents a single frame of an XRVideo, for rendering with Vulkan.
class VulkanXRVideoFrame : public XRVideoFrame {
 public:
  inline VulkanXRVideoFrame() {}
  
  inline VulkanXRVideoFrame(VulkanXRVideoFrame&& other) = default;
  inline VulkanXRVideoFrame& operator= (VulkanXRVideoFrame&& other) = default;
  
  inline VulkanXRVideoFrame(const VulkanXRVideoFrame& other) = delete;
  inline VulkanXRVideoFrame& operator= (const VulkanXRVideoFrame& other) = delete;
  
  void Configure(
      VulkanDevice* device);
  
  bool Initialize(
      const XRVideoFrameMetadata& metadata,
      const u8* contentPtr,
      TextureFramePromise* textureFramePromise,
      XRVideoDecodingContext* decodingContext,
      bool verboseDecoding);
  void Destroy();
  
  void WaitForResourceTransfers();
  
  void EnsureResourcesAreInGraphicsQueueFamily(VulkanCommandBuffer* cmdBuf);
  
  /// Must be called before starting the render pass for rendering.
  /// If this frame is a keyframe, then lastKeyframe may be nullptr.
  void PrepareRender(
      VulkanCommandBuffer* cmdBuf,
      vulkan_xrvideo_shader::DescriptorSet0* descriptorSet,
      const VkDescriptorBufferInfo& interpolatedDeformationStateBufferInfo,
      VulkanXRVideoFrame* lastKeyframe);
  
  /// Adds the rendering commands to the command buffer.
  /// If this frame is a keyframe, then lastKeyframe may be nullptr.
  /// Note: The pose in the uniform buffer must be set externally.
  ///       This is to allow for late updates when using OpenXR,
  ///       where it makes sense to estimate the predicted render pose as late as possible.
  void Render(
      VulkanCommandBuffer* cmdBuf,
      bool useSurfaceNormalShading,
      vulkan_xrvideo_shader::DescriptorSet0* descriptorSet,
      VulkanBuffer* uniformBuffer,
      const VulkanXRVideoFrame* lastKeyframe,
      const VulkanXRVideo* xrVideo) const;
  
  inline const VkDescriptorBufferInfo& GetDeformationStateBufferDesc() const { return deformationStateBufferDesc; }
  
 private:
  /// Vertex buffer on the GPU (only used by keyframes)
  VulkanBuffer vertexBuffer;
  
  /// Index buffer on the GPU (only used by keyframes)
  VulkanBuffer indexBuffer;
  
  /// Deformation state buffer on the GPU
  VulkanBuffer storageBuffer;
  
  /// Vertex alpha buffer on the GPU
  VulkanBuffer vertexAlphaBuffer;
  
  /// Descriptor for the part of storageBuffer that contains the deformation state.
  VkDescriptorBufferInfo deformationStateBufferDesc;
  
  VulkanTexture textureLuma;
  VulkanTexture textureChromaU;
  VulkanTexture textureChromaV;
  
  VulkanTexture textureRGB;
  
  // Staging buffers
  VulkanBuffer verticesStagingBuffer;
  VulkanBuffer indicesStagingBuffer;
  
  VulkanBuffer storageStagingBuffer;
  VulkanBuffer alphaStagingBuffer;
  VulkanBuffer textureStagingBuffer;
  
  shared_ptr<VulkanCommandBuffer> transferCmdBuf;
  VulkanFence transferFence;
  
  bool ownedByGraphicsQueueFamily = false;
  
  VulkanCommandPool transferPool;
  VulkanDevice* device;
};

}

#endif
