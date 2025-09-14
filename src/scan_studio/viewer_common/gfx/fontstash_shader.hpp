#pragma once

#include <memory>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/vulkan/libvis.h>

#ifdef HAVE_VULKAN
  #include <libvis/vulkan/descriptors.h>
  #include <libvis/vulkan/pipeline.h>
#endif

#ifdef __APPLE__
  #include <Metal/Metal.hpp>
#endif

#ifdef HAVE_OPENGL
  #include "scan_studio/viewer_common/opengl/shader.hpp"
#endif

namespace scan_studio {
using namespace vis;

struct RenderState;

struct FontStashShaderImpl {
  virtual inline ~FontStashShaderImpl() {}
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) = 0;
  virtual void Destroy() = 0;
};

struct FontStashShaderVulkan;
struct FontStashShaderMetal;
struct FontStashShaderOpenGL;

class FontStashShader {
 public:
  ~FontStashShader();
  
  bool Initialize(bool enableDepthTesting, RenderState* renderState);
  void Destroy() { if (impl) { impl->Destroy(); } }
  
  inline FontStashShaderVulkan* GetVulkanImpl() { return reinterpret_cast<FontStashShaderVulkan*>(impl); }
  inline FontStashShaderMetal* GetMetalImpl() { return reinterpret_cast<FontStashShaderMetal*>(impl); }
  inline FontStashShaderOpenGL* GetOpenGLImpl() { return reinterpret_cast<FontStashShaderOpenGL*>(impl); }
  
 private:
  FontStashShaderImpl* impl = nullptr;  // owned pointer
  
  bool enableDepthTesting;
};

#ifdef HAVE_VULKAN
struct FontStashShaderVulkan : public FontStashShaderImpl {
  ~FontStashShaderVulkan();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  virtual void Destroy() override;
  
  VulkanDescriptorSetLayout perTextureDescriptorSetLayout;
  VulkanDescriptorSetLayout perFrameDescriptorSetLayout;
  VulkanPipelineLayout pipelineLayout;
  VulkanPipeline pipeline;
};
#endif

#ifdef __APPLE__
struct FontStashShaderMetal : public FontStashShaderImpl {
  ~FontStashShaderMetal();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  virtual void Destroy() override;
  
  NS::SharedPtr<MTL::RenderPipelineState> renderPipelineState;
  NS::SharedPtr<MTL::DepthStencilState> depthStencilState;
};
#endif

#ifdef HAVE_OPENGL
struct FontStashShaderOpenGL : public FontStashShaderImpl {
  ~FontStashShaderOpenGL();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  virtual void Destroy() override;
  
  shared_ptr<ShaderProgram> program;
  
  GLint modelViewProjectionLocation;
  GLint fontTextureLocation;
};
#endif

}
