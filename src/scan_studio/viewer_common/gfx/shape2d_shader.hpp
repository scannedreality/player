#pragma once

#include <memory>

#ifdef HAVE_OPENGL
  #if defined(_WIN32) || defined(__APPLE__)
    #include <SDL_opengl.h>
  #else
    #include <GLES2/gl2.h>
  #endif
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

struct Shape2DShaderImpl {
  virtual inline ~Shape2DShaderImpl() {}
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
};

struct Shape2DShaderVulkan;
struct Shape2DShaderMetal;
struct Shape2DShaderOpenGL;

class Shape2DShader {
 public:
  ~Shape2DShader();
  
  bool Initialize(bool enableDepthTesting, RenderState* renderState);
  
  inline void Destroy() { if (impl) { impl->Destroy(); } }
  
  inline Shape2DShaderVulkan* GetVulkanImpl() { return reinterpret_cast<Shape2DShaderVulkan*>(impl); }
  inline Shape2DShaderMetal* GetMetalImpl() { return reinterpret_cast<Shape2DShaderMetal*>(impl); }
  inline Shape2DShaderOpenGL* GetOpenGLImpl() { return reinterpret_cast<Shape2DShaderOpenGL*>(impl); }
  
  inline bool DepthTestingEnabled() const { return enableDepthTesting; }
  
 private:
  Shape2DShaderImpl* impl = nullptr;  // owned pointer
  
  bool enableDepthTesting;
};

#ifdef HAVE_VULKAN
struct Shape2DShaderVulkan : public Shape2DShaderImpl {
  ~Shape2DShaderVulkan();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  VulkanDescriptorSetLayout descriptorSetLayout;
  VulkanPipelineLayout pipelineLayout;
  VulkanPipeline pipeline;
};
#endif

#ifdef __APPLE__
struct Shape2DShaderMetal : public Shape2DShaderImpl {
  ~Shape2DShaderMetal();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  NS::SharedPtr<MTL::RenderPipelineState> renderPipelineState;
  NS::SharedPtr<MTL::DepthStencilState> depthStencilState;
};
#endif

#ifdef HAVE_OPENGL
struct Shape2DShaderOpenGL : public Shape2DShaderImpl {
  ~Shape2DShaderOpenGL();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  shared_ptr<ShaderProgram> program;
  
  GLint modelViewProjectionLocation;
  GLint colorLocation;
};
#endif

}
