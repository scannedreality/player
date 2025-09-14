#include "scan_studio/viewer_common/gfx/textured_shape2d_shader.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#ifdef HAVE_VULKAN
  #include <libvis/vulkan/device.h>
  #include <libvis/vulkan/render_pass.h>
  #include <libvis/vulkan/shader.h>
  
  // Generated shader bytecode
  #include "scan_studio/viewer_common/generated/gfx/textured_shape2d_vulkan_shader.binding.hpp"
  #include "scan_studio/viewer_common/generated/gfx/textured_shape2d_vulkan_shader.frag.h"
  #include "scan_studio/viewer_common/generated/gfx/textured_shape2d_vulkan_shader.vert.h"
#endif

namespace scan_studio {

TexturedShape2DShader::~TexturedShape2DShader() {
  Destroy();
}

bool TexturedShape2DShader::Initialize(bool enableDepthTesting, RenderState* renderState) {
  this->enableDepthTesting = enableDepthTesting;
  
  delete impl;
  impl = nullptr;
  
  switch (renderState->api) {
  case RenderState::RenderingAPI::Vulkan:
    #ifdef HAVE_VULKAN
      impl = new TexturedShape2DShaderVulkan();
    #else
      LOG(ERROR) << "Application was compiled without Vulkan support";
    #endif
    break;
  case RenderState::RenderingAPI::Metal:
    LOG(ERROR) << "TexturedShape2DShader is not implemented for the Metal render path yet";
    break;
  case RenderState::RenderingAPI::OpenGL:
    LOG(FATAL) << "TexturedShape2DShaderOpenGL is not implemented yet";
    // impl = new TexturedShape2DShaderOpenGL();
    break;
  case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return false;
  }
  
  if (!impl) { LOG(ERROR) << "Allocating a TexturedShape2DShader impl failed"; return false; }
  
  return impl->Initialize(enableDepthTesting, renderState);
}

#ifdef HAVE_VULKAN
TexturedShape2DShaderVulkan::~TexturedShape2DShaderVulkan() {
  Destroy();
}

bool TexturedShape2DShaderVulkan::Initialize(bool enableDepthTesting, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  
  if (!textured_shape2d_vulkan_shader::CreateDescriptorSet0Layout(&perViewDescriptorSetLayout, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the per-view descriptor set layout";
    Destroy(); return false;
  }
  if (!textured_shape2d_vulkan_shader::CreateDescriptorSet1Layout(&perFrameDescriptorSetLayout, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the per-frame descriptor set layout";
    Destroy(); return false;
  }
  
  pipelineLayout.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, /*offset*/ 0, /*size*/ 4 * sizeof(float));
  pipelineLayout.AddDescriptorSetLayout(perViewDescriptorSetLayout);
  pipelineLayout.AddDescriptorSetLayout(perFrameDescriptorSetLayout);
  if (!pipelineLayout.Initialize(*vulkanState->device)) { LOG(ERROR) << "Failed to initialize pipeline layout"; Destroy(); return false; }
  
  array<VulkanShader, 2> shaders;
  if (!shaders[0].Initialize(textured_shape2d_vulkan_shader_vert, VK_SHADER_STAGE_VERTEX_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load textured_shape2d_vulkan_shader_vert"; Destroy(); return false;
  }
  if (!shaders[1].Initialize(textured_shape2d_vulkan_shader_frag, VK_SHADER_STAGE_FRAGMENT_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load textured_shape2d_vulkan_shader_frag"; Destroy(); return false;
  }
  
  pipeline.AddShaderStage(shaders[0]);
  pipeline.AddShaderStage(shaders[1]);
  
  pipeline.SetBasicParameters(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, enableDepthTesting ? VK_TRUE : VK_FALSE, VK_FALSE);
  // Using premultiplied alpha to get alpha output to the render target, with good behavior in (bilinear) interpolation.
  // Alpha output is necessary for blending with the passthrough layer in the Quest viewer.
  // https://apoorvaj.io/alpha-compositing-opengl-blending-and-premultiplied-alpha/#premultiplied-alpha
  pipeline.UsePremultipliedAlphaBlending();
  // We always output color to attachment 0, so we can get the relevant sample count from it
  pipeline.multisample_info().rasterizationSamples = vulkanState->renderPass->Attachments()[0].samples;
  
  pipeline.AddVertexBindingDescription(VkVertexInputBindingDescription{0, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX});
  pipeline.AddVertexAttributeDescription({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
  pipeline.AddVertexAttributeDescription({1, 0, VK_FORMAT_R32G32_SFLOAT, 2 * sizeof(float)});
  
  if (!pipeline.Initialize(pipelineLayout, vulkanState->device->pipeline_cache(), *vulkanState->renderPass, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the TexturedShape2D graphics pipeline";
    Destroy(); return false;
  }
  
  return true;
}

void TexturedShape2DShaderVulkan::Destroy() {
  pipeline.Destroy();
  pipelineLayout.Destroy();
  perViewDescriptorSetLayout.Destroy();
  perFrameDescriptorSetLayout.Destroy();
}
#endif

#ifdef HAVE_OPENGL
// bool TexturedShape2DShaderOpenGL::Initialize(bool /*enableDepthTesting*/, RenderState* /*renderState*/) {
//   program.reset(new ShaderProgram());
//   
//   const string vertexShaderSrc = R"SHADERCODE(#version 100
// precision highp float;
// 
// attribute vec2 in_position;
// 
// uniform highp mat4 modelViewProjection;
// 
// void main() {
//   gl_Position = modelViewProjection * vec4(in_position.xy, 0.0, 1.0);
// }
// )SHADERCODE";
//   CHECK(program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader));
//   
//   const string fragmentShaderSrc = R"SHADERCODE(#version 100
// precision highp float;
// 
// uniform highp vec4 color;
// 
// void main() {
//   gl_FragColor = color;
// }
// )SHADERCODE";
//   CHECK(program->AttachShader(fragmentShaderSrc.c_str(), ShaderProgram::ShaderType::kFragmentShader));
//   
//   LOG(1) << "TexturedShape2DShaderOpenGL: Linking shader ...";
//   CHECK(program->LinkProgram());
//   
//   program->UseProgram();
//   
//   modelViewProjectionLocation = program->GetUniformLocationOrAbort("modelViewProjection");
//   colorLocation = program->GetUniformLocationOrAbort("color");
//   
//   return true;
// }
// 
// void TexturedShape2DShaderOpenGL::Destroy() {
//   program.reset();
// }
#endif

}
