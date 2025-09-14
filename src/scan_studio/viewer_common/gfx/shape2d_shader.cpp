#include "scan_studio/viewer_common/gfx/shape2d_shader.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#ifdef HAVE_VULKAN
  #include <libvis/vulkan/device.h>
  #include <libvis/vulkan/render_pass.h>
  #include <libvis/vulkan/shader.h>
  
  // Generated shader bytecode
  #include "scan_studio/viewer_common/generated/gfx/shape2d_vulkan_shader.binding.hpp"
  #include "scan_studio/viewer_common/generated/gfx/shape2d_vulkan_shader.frag.h"
  #include "scan_studio/viewer_common/generated/gfx/shape2d_vulkan_shader.vert.h"
#endif

#ifdef __APPLE__
  #include <QuartzCore/QuartzCore.hpp>
  
  #include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"
  
  #include "scan_studio/viewer_common/metal/library_cache.hpp"
  #include "scan_studio/viewer_common/gfx/shape2d_metal_shader.h"
  
  // Generated shader bytecode
  #include "viewer_common_metallib.h"
#endif

namespace scan_studio {

Shape2DShader::~Shape2DShader() {
  Destroy();
}

bool Shape2DShader::Initialize(bool enableDepthTesting, RenderState* renderState) {
  this->enableDepthTesting = enableDepthTesting;
  
  delete impl;
  impl = nullptr;
  
  switch (renderState->api) {
  case RenderState::RenderingAPI::Vulkan:
    #ifdef HAVE_VULKAN
      impl = new Shape2DShaderVulkan();
    #else
      LOG(ERROR) << "Application was compiled without Vulkan support";
    #endif
    break;
  case RenderState::RenderingAPI::Metal:
    #ifdef __APPLE__
      impl = new Shape2DShaderMetal();
    #else
      LOG(ERROR) << "Application was compiled without Metal support";
    #endif
    break;
  case RenderState::RenderingAPI::OpenGL:
    #ifdef HAVE_OPENGL
      impl = new Shape2DShaderOpenGL();
    #else
      LOG(ERROR) << "Application was compiled without OpenGL support";
    #endif
    break;
  case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return false;
  }
  
  if (!impl) { LOG(ERROR) << "Allocating a Shape2DShader impl failed"; return false; }
  
  return impl->Initialize(enableDepthTesting, renderState);
}


#ifdef HAVE_VULKAN
Shape2DShaderVulkan::~Shape2DShaderVulkan() {
  Destroy();
}

bool Shape2DShaderVulkan::Initialize(bool enableDepthTesting, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  
  if (!shape2d_vulkan_shader::CreateDescriptorSet0Layout(&descriptorSetLayout, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the descriptor set layout";
    Destroy(); return false;
  }
  
  pipelineLayout.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, /*offset*/ 0, /*size*/ 4 * sizeof(float));
  pipelineLayout.AddDescriptorSetLayout(descriptorSetLayout);
  if (!pipelineLayout.Initialize(*vulkanState->device)) { LOG(ERROR) << "Failed to initialize pipeline layout"; Destroy(); return false; }
  
  array<VulkanShader, 2> shaders;
  if (!shaders[0].Initialize(shape2d_vulkan_shader_vert, VK_SHADER_STAGE_VERTEX_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load shape2d_vulkan_shader_vert"; Destroy(); return false;
  }
  if (!shaders[1].Initialize(shape2d_vulkan_shader_frag, VK_SHADER_STAGE_FRAGMENT_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load shape2d_vulkan_shader_frag"; Destroy(); return false;
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
  
  pipeline.AddVertexBindingDescription(VkVertexInputBindingDescription{0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX});
  pipeline.AddVertexAttributeDescription({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
  
  if (!pipeline.Initialize(pipelineLayout, vulkanState->device->pipeline_cache(), *vulkanState->renderPass, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the Shape2D graphics pipeline";
    Destroy(); return false;
  }
  
  return true;
}

void Shape2DShaderVulkan::Destroy() {
  pipeline.Destroy();
  pipelineLayout.Destroy();
  descriptorSetLayout.Destroy();
}
#endif


#ifdef __APPLE__
Shape2DShaderMetal::~Shape2DShaderMetal() {
  Destroy();
}

bool Shape2DShaderMetal::Initialize(bool enableDepthTesting, RenderState* renderState) {
  if (enableDepthTesting) {
    LOG(ERROR) << "The Metal render path does not support depth testing in Shape2DShader yet";
    return false;
  }
  
  MetalRenderState* metalState = renderState->AsMetalRenderState();
  
  auto pipelineDesc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
  pipelineDesc->setLabel(NS::String::string("Shape2DRenderPipeline", NS::UTF8StringEncoding));
  
  auto shaderFunctions = metalState->libraryCache->AssignVertexAndFragmentFunctions(
      "Shape2D_VertexMain", "Shape2D_FragmentMain", pipelineDesc.get(),
      "viewer_common", viewer_common_metallib, viewer_common_metallib_len, metalState->device);
  if (!shaderFunctions) { return false; }
  
  auto colorAttachmentDesc = pipelineDesc->colorAttachments()->object(0);
  colorAttachmentDesc->setPixelFormat(metalState->renderPassDesc->colorFormats[0]);
  // Using premultiplied alpha to get alpha output to the render target, with good behavior in (bilinear) interpolation.
  // Alpha output is necessary for blending with the passthrough layer in the Quest viewer.
  // https://apoorvaj.io/alpha-compositing-opengl-blending-and-premultiplied-alpha/#premultiplied-alpha
  colorAttachmentDesc->setBlendingEnabled(true);
  colorAttachmentDesc->setSourceRGBBlendFactor(MTL::BlendFactorOne);
  colorAttachmentDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  colorAttachmentDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
  colorAttachmentDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
  colorAttachmentDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  colorAttachmentDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
  
  pipelineDesc->setDepthAttachmentPixelFormat(metalState->renderPassDesc->depthFormat);
  
  pipelineDesc->vertexBuffers()->object(Shape2D_VertexInputIndex_vertices)->setMutability(MTL::MutabilityImmutable);
  pipelineDesc->vertexBuffers()->object(Shape2D_VertexInputIndex_instanceData)->setMutability(MTL::MutabilityImmutable);
  
  pipelineDesc->fragmentBuffers()->object(Shape2D_FragmentInputIndex_instanceData)->setMutability(MTL::MutabilityImmutable);
  
  NS::Error* err;
  renderPipelineState = NS::TransferPtr(metalState->device->newRenderPipelineState(pipelineDesc.get(), &err));
  if (!renderPipelineState) {
    LOG(ERROR) << "Failed to create render pipeline: " << err->localizedDescription()->utf8String();
    Destroy(); return false;
  }
  
  // Initialize depth-stencil state
  auto depthStencilDesc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
  depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  depthStencilDesc->setDepthWriteEnabled(false);
  depthStencilState = NS::TransferPtr(metalState->device->newDepthStencilState(depthStencilDesc.get()));
  
  return true;
}

void Shape2DShaderMetal::Destroy() {
  renderPipelineState.reset();
}
#endif


#ifdef HAVE_OPENGL
Shape2DShaderOpenGL::~Shape2DShaderOpenGL() {
  Destroy();
}

bool Shape2DShaderOpenGL::Initialize(bool enableDepthTesting, RenderState* /*renderState*/) {
  if (enableDepthTesting) {
    LOG(ERROR) << "The OpenGL render path does not support depth testing in Shape2DShader yet";
    return false;
  }
  
  program.reset(new ShaderProgram());
  
  const string vertexShaderSrc = R"SHADERCODE(#version 100
precision highp float;

attribute vec2 in_position;

uniform highp mat4 modelViewProjection;

void main() {
  gl_Position = modelViewProjection * vec4(in_position.xy, 0.0, 1.0);
}
)SHADERCODE";
  CHECK(program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader));
  
  const string fragmentShaderSrc = R"SHADERCODE(#version 100
precision highp float;

uniform highp vec4 color;

void main() {
  gl_FragColor = color;
}
)SHADERCODE";
  CHECK(program->AttachShader(fragmentShaderSrc.c_str(), ShaderProgram::ShaderType::kFragmentShader));
  
  // LOG(1) << "Shape2DShaderOpenGL: Linking shader ...";
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  modelViewProjectionLocation = program->GetUniformLocationOrAbort("modelViewProjection");
  colorLocation = program->GetUniformLocationOrAbort("color");
  
  return true;
}

void Shape2DShaderOpenGL::Destroy() {
  program.reset();
}
#endif

}
