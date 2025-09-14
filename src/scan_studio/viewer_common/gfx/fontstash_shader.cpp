#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"

#ifdef HAVE_VULKAN
  #include <libvis/vulkan/device.h>
  #include <libvis/vulkan/render_pass.h>
  #include <libvis/vulkan/shader.h>
  
  // Generated shader bytecode
  #include "scan_studio/viewer_common/generated/gfx/fontstash_vulkan_shader.binding.hpp"
  #include "scan_studio/viewer_common/generated/gfx/fontstash_vulkan_shader.frag.h"
  #include "scan_studio/viewer_common/generated/gfx/fontstash_vulkan_shader.vert.h"
#endif

#ifdef __APPLE__
  #include <QuartzCore/QuartzCore.hpp>
  
  #include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"
  
  #include "scan_studio/viewer_common/metal/library_cache.hpp"
  #include "scan_studio/viewer_common/gfx/fontstash_metal_shader.h"
  
  // Generated shader bytecode
  #include "viewer_common_metallib.h"
#endif

namespace scan_studio {

FontStashShader::~FontStashShader() {
  Destroy();
}

bool FontStashShader::Initialize(bool enableDepthTesting, RenderState* renderState) {
  this->enableDepthTesting = enableDepthTesting;
  
  delete impl;
  
  switch (renderState->api) {
  #ifdef HAVE_VULKAN
    case RenderState::RenderingAPI::Vulkan: impl = new FontStashShaderVulkan(); break;
  #else
    case RenderState::RenderingAPI::Vulkan: LOG(FATAL) << "Application was compiled without Vulkan support"; break;
  #endif
  #ifdef __APPLE__
    case RenderState::RenderingAPI::Metal: impl = new FontStashShaderMetal(); break;
  #else
    case RenderState::RenderingAPI::Metal: LOG(FATAL) << "Application was compiled without Metal support"; break;
  #endif
  #ifdef HAVE_OPENGL
    case RenderState::RenderingAPI::OpenGL: impl = new FontStashShaderOpenGL(); break;
  #else
    case RenderState::RenderingAPI::OpenGL: LOG(FATAL) << "Application was compiled without OpenGL support"; break;
  #endif
    case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return false;
  }
  
  if (!impl) { LOG(ERROR) << "Allocating a FontStashShader impl failed"; return false; }
  
  return impl->Initialize(enableDepthTesting, renderState);
}


#ifdef HAVE_VULKAN
FontStashShaderVulkan::~FontStashShaderVulkan() {
  Destroy();
}

bool FontStashShaderVulkan::Initialize(bool enableDepthTesting, RenderState* renderState) {
  VulkanRenderState* vulkanState = renderState->AsVulkanRenderState();
  
  if (!fontstash_vulkan_shader::CreateDescriptorSet0Layout(&perTextureDescriptorSetLayout, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the descriptor set layout 0";
    Destroy(); return false;
  }
  
  if (!fontstash_vulkan_shader::CreateDescriptorSet1Layout(&perFrameDescriptorSetLayout, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the descriptor set layout 1";
    Destroy(); return false;
  }
  
  pipelineLayout.AddDescriptorSetLayout(perTextureDescriptorSetLayout);
  pipelineLayout.AddDescriptorSetLayout(perFrameDescriptorSetLayout);
  if (!pipelineLayout.Initialize(*vulkanState->device)) { LOG(ERROR) << "Failed to initialize pipeline layout"; Destroy(); return false; }
  
  array<VulkanShader, 2> shaders;
  if (!shaders[0].Initialize(fontstash_vulkan_shader_vert, VK_SHADER_STAGE_VERTEX_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load fontstash_vulkan_shader_vert"; Destroy(); return false;
  }
  if (!shaders[1].Initialize(fontstash_vulkan_shader_frag, VK_SHADER_STAGE_FRAGMENT_BIT, *vulkanState->device)) {
    LOG(ERROR) << "Failed to load fontstash_vulkan_shader_frag"; Destroy(); return false;
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
  
  pipeline.AddVertexBindingDescription(VkVertexInputBindingDescription{0, sizeof(FONSVertex), VK_VERTEX_INPUT_RATE_VERTEX});
  u32 offset = 0;
  pipeline.AddVertexAttributeDescription({0, 0, VK_FORMAT_R32G32_SFLOAT, offset});
  offset += 2 * sizeof(float);
  pipeline.AddVertexAttributeDescription({1, 0, VK_FORMAT_R32G32_SFLOAT, offset});
  offset += 2 * sizeof(float);
  pipeline.AddVertexAttributeDescription({2, 0, VK_FORMAT_R8G8B8A8_UNORM, offset});
  offset += 4;
  
  if (!pipeline.Initialize(pipelineLayout, vulkanState->device->pipeline_cache(), *vulkanState->renderPass, *vulkanState->device)) {
    LOG(ERROR) << "Failed to initialize the FontStash graphics pipeline";
    Destroy(); return false;
  }
  
  return true;
}

void FontStashShaderVulkan::Destroy() {
  pipeline.Destroy();
  pipelineLayout.Destroy();
  perTextureDescriptorSetLayout.Destroy();
  perFrameDescriptorSetLayout.Destroy();
}
#endif


#ifdef __APPLE__
FontStashShaderMetal::~FontStashShaderMetal() {
  Destroy();
}

bool FontStashShaderMetal::Initialize(bool enableDepthTesting, RenderState* renderState) {
  if (enableDepthTesting) {
    LOG(ERROR) << "The Metal render path does not support depth testing in FontStashShader yet";
    return false;
  }
  
  MetalRenderState* metalState = renderState->AsMetalRenderState();
  
  auto pipelineDesc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
  pipelineDesc->setLabel(NS::String::string("Text2DRenderPipeline", NS::UTF8StringEncoding));
  
  auto shaderFunctions = metalState->libraryCache->AssignVertexAndFragmentFunctions(
      "Fontstash_VertexMain", "Fontstash_FragmentMain", pipelineDesc.get(),
      "viewer_common", viewer_common_metallib, viewer_common_metallib_len, metalState->device);
  if (!shaderFunctions) { return false; }
  
  auto colorAttachmentDesc = pipelineDesc->colorAttachments()->object(0);
  colorAttachmentDesc->setPixelFormat(metalState->renderPassDesc->colorFormats[0]);
  // Using premultiplied alpha to get alpha output to the render target, with good behavior in (bilinear) interpolation.
  // Alpha output is necessary for blending with the passthrough layer in the Quest viewer.
  colorAttachmentDesc->setBlendingEnabled(true);
  colorAttachmentDesc->setSourceRGBBlendFactor(MTL::BlendFactorOne);
  colorAttachmentDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  colorAttachmentDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
  colorAttachmentDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
  colorAttachmentDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  colorAttachmentDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
  
  pipelineDesc->setDepthAttachmentPixelFormat(metalState->renderPassDesc->depthFormat);
  
  pipelineDesc->vertexBuffers()->object(Fontstash_VertexInputIndex_vertices)->setMutability(MTL::MutabilityImmutable);
  pipelineDesc->vertexBuffers()->object(Fontstash_VertexInputIndex_instanceData)->setMutability(MTL::MutabilityImmutable);
  
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

void FontStashShaderMetal::Destroy() {
  renderPipelineState.reset();
}
#endif


#ifdef HAVE_OPENGL
FontStashShaderOpenGL::~FontStashShaderOpenGL() {
  Destroy();
}

bool FontStashShaderOpenGL::Initialize(bool /*enableDepthTesting*/, RenderState* /*renderState*/) {
  program.reset(new ShaderProgram());
  
  string vertexShaderSrc = R"SHADERCODE(#version 100
precision highp float;

uniform highp mat4 modelViewProjection;

attribute vec2 in_position;
attribute vec4 in_color;
attribute vec2 in_texcoord;

varying vec4 vColor;
varying vec2 vTexcoord;

void main() {
  vColor = in_color;
  vTexcoord = in_texcoord;
  gl_Position = modelViewProjection * vec4(in_position.xy, 0.0, 1.0);
}
)SHADERCODE";
  CHECK(program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader));
  
  string fragmentShaderSrc = R"SHADERCODE(#version 100
precision highp float;

uniform sampler2D fontTexture;

varying vec4 vColor;
varying vec2 vTexcoord;

void main() {
  float alpha = texture2D(fontTexture, vTexcoord).x;
  gl_FragColor = vColor * vec4(alpha, alpha, alpha, alpha);
}
)SHADERCODE";
  CHECK(program->AttachShader(fragmentShaderSrc.c_str(), ShaderProgram::ShaderType::kFragmentShader));
  
  // LOG(1) << "FontStashShaderOpenGL: Linking shader ...";
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  modelViewProjectionLocation = program->GetUniformLocationOrAbort("modelViewProjection");
  fontTextureLocation = program->GetUniformLocationOrAbort("fontTexture");
  
  return true;
}

void FontStashShaderOpenGL::Destroy() {
  program.reset();
}
#endif

}
