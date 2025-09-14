#pragma once
#ifdef HAVE_VULKAN

#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/pipeline.h>

#include "scan_studio/viewer_common/xrvideo/xrvideo_common_resources.hpp"

// Generated shader bindings
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_interpolate_deformation_state.binding.hpp"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_interpolate_deformation_state_from_identity.binding.hpp"

namespace scan_studio {
using namespace vis;

/// Initializes and stores common resources that are required for rendering XRVideos,
/// for example, compiled shaders.
// TODO: Due to having descriptor sets (and descriptor pools) stored in the xyz::Program members,
//       it is not possible to use multiple VulkanXRVideo instances with the same VulkanXRVideoCommonResources object yet!
class VulkanXRVideoCommonResources : public XRVideoCommonResources {
 friend class VulkanXRVideo;
 friend class VulkanXRVideoFrame;
 friend class VulkanXRVideoRenderLock;
 public:
  /// Default constructor, does not initialize the object.
  VulkanXRVideoCommonResources() = default;
  
  VulkanXRVideoCommonResources(const VulkanXRVideoCommonResources& other) = delete;
  VulkanXRVideoCommonResources& operator= (const VulkanXRVideoCommonResources& other) = delete;
  
  ~VulkanXRVideoCommonResources();
  
  bool Initialize(
      int framesInFlightCount,
      VkSampleCountFlagBits msaaSamples,
      VkRenderPass renderPass,
      VulkanDevice* device);
  void Destroy();
  
 private:
  // Programs for deformation state interpolation
  vulkan_interpolate_deformation_state::Program interpolateDeformationState;
  vulkan_interpolate_deformation_state_from_identity::Program interpolateDeformationStateFromIdentity;
  
  // Pipeline for default rendering
  VulkanDescriptorSetLayout descriptorSetLayout;
  VulkanPipelineLayout pipelineLayout;
  VulkanPipeline pipeline;
  
  // Pipeline for alpha blended rendering
  VulkanPipeline alphaPipeline;
  
  // Pipeline for rendering with normal-based shading
  VulkanPipelineLayout normalShadingPipelineLayout;
  VulkanPipeline normalShadingPipeline;
};

}

#endif
