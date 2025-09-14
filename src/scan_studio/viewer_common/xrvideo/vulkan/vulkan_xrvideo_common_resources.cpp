#ifdef HAVE_VULKAN
#include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_common_resources.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader.binding.hpp"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader.frag.h"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader.vert.h"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader_alpha_blending.frag.h"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader_alpha_blending.vert.h"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader_normal_shading.frag.h"
#include "scan_studio/viewer_common/generated/xrvideo/vulkan_xrvideo_shader_normal_shading.vert.h"

namespace scan_studio {

VulkanXRVideoCommonResources::~VulkanXRVideoCommonResources() {
  Destroy();
}

bool VulkanXRVideoCommonResources::Initialize(
    int framesInFlightCount,
    VkSampleCountFlagBits msaaSamples,
    VkRenderPass renderPass,
    VulkanDevice* device) {
  // Initialize graphics pipeline for rendering
  if (!vulkan_xrvideo_shader::CreateDescriptorSet0Layout(&descriptorSetLayout, *device)) {
    LOG(ERROR) << "Failed to initialize the descriptor set layout";
    Destroy(); return false;
  }
  
  pipelineLayout.AddDescriptorSetLayout(descriptorSetLayout);
  pipelineLayout.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, 3 * sizeof(float));
  if (!pipelineLayout.Initialize(*device)) {
    LOG(ERROR) << "Failed to initialize pipelineLayout";
    Destroy(); return false;
  }
  
  normalShadingPipelineLayout.AddDescriptorSetLayout(descriptorSetLayout);
  if (!normalShadingPipelineLayout.Initialize(*device)) {
    LOG(ERROR) << "Failed to initialize normalShadingPipelineLayout";
    Destroy(); return false;
  }
  
  for (VulkanPipeline* pipe : {&pipeline, &alphaPipeline, &normalShadingPipeline}) {
    usize vertexShaderSize;
    const void* vertexShaderData;
    usize fragmentShaderSize;
    const void* fragmentShaderData;
    VulkanPipelineLayout* pipeLayout;
    
    if (pipe == &pipeline) {
      vertexShaderSize = vulkan_xrvideo_shader_vert.size();
      vertexShaderData = vulkan_xrvideo_shader_vert.data();
      fragmentShaderSize = vulkan_xrvideo_shader_frag.size();
      fragmentShaderData = vulkan_xrvideo_shader_frag.data();
      pipeLayout = &pipelineLayout;
    } else if (pipe == &alphaPipeline) {
      vertexShaderSize = vulkan_xrvideo_shader_alpha_blending_vert.size();
      vertexShaderData = vulkan_xrvideo_shader_alpha_blending_vert.data();
      fragmentShaderSize = vulkan_xrvideo_shader_alpha_blending_frag.size();
      fragmentShaderData = vulkan_xrvideo_shader_alpha_blending_frag.data();
      pipeLayout = &pipelineLayout;
    } else {  // if (pipe == &normalShadingPipeline) {
      vertexShaderSize = vulkan_xrvideo_shader_normal_shading_vert.size();
      vertexShaderData = vulkan_xrvideo_shader_normal_shading_vert.data();
      fragmentShaderSize = vulkan_xrvideo_shader_normal_shading_frag.size();
      fragmentShaderData = vulkan_xrvideo_shader_normal_shading_frag.data();
      pipeLayout = &normalShadingPipelineLayout;
    }
    
    if (!pipe->LoadShaderStage(vertexShaderSize, vertexShaderData, VK_SHADER_STAGE_VERTEX_BIT, *device)) {
      LOG(ERROR) << "Failed to load XRVideo vertex shader"; Destroy(); return false;
    }
    if (!pipe->LoadShaderStage(fragmentShaderSize, fragmentShaderData, VK_SHADER_STAGE_FRAGMENT_BIT, *device)) {
      LOG(ERROR) << "Failed to load XRVideo fragment shader"; Destroy(); return false;
    }
    
    pipe->SetBasicParameters(
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        /*depth_test_enable*/ VK_TRUE,
        /*depth_write_enable*/ VK_TRUE);
    pipe->multisample_info().rasterizationSamples = msaaSamples;
    if (pipe == &alphaPipeline) {
      pipe->UsePremultipliedAlphaBlending();
    }
    
    pipe->AddVertexBindingDescription(VkVertexInputBindingDescription{0, sizeof(XRVideoVertex), VK_VERTEX_INPUT_RATE_VERTEX});
    u32 vertexAttribOffset = 0;
    // Position encoded as u16
    pipe->AddVertexAttributeDescription({0, 0, VK_FORMAT_R16G16B16A16_UINT, vertexAttribOffset});
    vertexAttribOffset += 4 * sizeof(u16);
    // Texture coordinates encoded as u16
    if (pipe != &normalShadingPipeline) {
      pipe->AddVertexAttributeDescription({1, 0, VK_FORMAT_R16G16_UINT, vertexAttribOffset});
    }
    vertexAttribOffset += 2 * sizeof(u16);
    // Deformation node indices
    pipe->AddVertexAttributeDescription({2, 0, VK_FORMAT_R16G16B16A16_UINT, vertexAttribOffset});
    vertexAttribOffset += 4 * sizeof(u16);
    // Deformation node weights
    pipe->AddVertexAttributeDescription({3, 0, VK_FORMAT_R8G8B8A8_UINT, vertexAttribOffset});
    vertexAttribOffset += 4 * sizeof(u8);
    
    if (pipe == &alphaPipeline) {
      // Use the vertex alphas provided from a separate vertex buffer (binding 1)
      pipe->AddVertexBindingDescription(VkVertexInputBindingDescription{1, sizeof(u8), VK_VERTEX_INPUT_RATE_VERTEX});
      pipe->AddVertexAttributeDescription({4, 1, VK_FORMAT_R8_UINT, 0});
    }
    
    if (!pipe->Initialize(*pipeLayout, device->pipeline_cache(), renderPass, *device)) {
      LOG(ERROR) << "Failed to initialize the mesh stream graphics pipeline";
      Destroy(); return false;
    }
  }
  
  // Initialize deformation state interpolation
  if (!interpolateDeformationState.Initialize(1024, device->pipeline_cache(), *device, framesInFlightCount)) { LOG(ERROR) << "Failed to initialize interpolateDeformationState"; return false; }
  if (!interpolateDeformationStateFromIdentity.Initialize(1024, device->pipeline_cache(), *device, framesInFlightCount)) { LOG(ERROR) << "Failed to initialize interpolateDeformationState"; return false; }
  
  return true;
}

void VulkanXRVideoCommonResources::Destroy() {
  pipeline.Destroy();
  pipelineLayout.Destroy();
  normalShadingPipeline.Destroy();
  normalShadingPipelineLayout.Destroy();
}

}

#endif
