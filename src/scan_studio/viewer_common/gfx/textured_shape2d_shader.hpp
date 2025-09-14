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

#ifdef HAVE_OPENGL
  #include "scan_studio/viewer_common/opengl/shader.hpp"
#endif

namespace scan_studio {
using namespace vis;

struct RenderState;

struct TexturedShape2DShaderImpl {
  virtual inline ~TexturedShape2DShaderImpl() {}
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
};

struct TexturedShape2DShaderVulkan;
struct TexturedShape2DShaderOpenGL;

class TexturedShape2DShader {
 public:
  ~TexturedShape2DShader();
  
  bool Initialize(bool enableDepthTesting, RenderState* renderState);
  
  inline void Destroy() { if (impl) { impl->Destroy(); } }
  
  inline TexturedShape2DShaderVulkan* GetVulkanImpl() { return reinterpret_cast<TexturedShape2DShaderVulkan*>(impl); }
  inline TexturedShape2DShaderOpenGL* GetOpenGLImpl() { return reinterpret_cast<TexturedShape2DShaderOpenGL*>(impl); }
  
  inline bool DepthTestingEnabled() const { return enableDepthTesting; }
  
 private:
  TexturedShape2DShaderImpl* impl = nullptr;  // owned pointer
  
  bool enableDepthTesting;
};

#ifdef HAVE_VULKAN
struct TexturedShape2DShaderVulkan : public TexturedShape2DShaderImpl {
  ~TexturedShape2DShaderVulkan();
  
  virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  VulkanDescriptorSetLayout perViewDescriptorSetLayout;
  VulkanDescriptorSetLayout perFrameDescriptorSetLayout;
  VulkanPipelineLayout pipelineLayout;
  VulkanPipeline pipeline;
};
#endif

#ifdef HAVE_OPENGL
// struct TexturedShape2DShaderOpenGL : public TexturedShape2DShaderImpl {
//   virtual bool Initialize(bool enableDepthTesting, RenderState* renderState) override;
//   
//   virtual void Destroy() override;
//   
//   shared_ptr<ShaderProgram> program;
//   
//   GLint modelViewProjectionLocation;
//   GLint colorLocation;
// };
#endif

}
