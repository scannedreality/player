#include "scan_studio/viewer_common/gfx/fontstash.hpp"

#include "scan_studio/viewer_common/gfx/fontstash_opengl.hpp"
#ifdef HAVE_VULKAN
  #include "scan_studio/viewer_common/gfx/fontstash_vulkan.hpp"
#endif
#ifdef __APPLE__
  #include "scan_studio/viewer_common/gfx/fontstash_metal.hpp"
#endif

#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

FontStash* FontStash::Create(int textureWidth, int textureHeight, FontStashShader* shader, RenderState* renderState) {
  FontStash* impl = nullptr;
  
  switch (renderState->api) {
  #ifdef HAVE_VULKAN
    case RenderState::RenderingAPI::Vulkan: impl = new FontStashVulkan(); break;
  #else
    case RenderState::RenderingAPI::Vulkan: LOG(ERROR) << "Application was compiled without Vulkan support"; return nullptr;
  #endif
  #ifdef __APPLE__
    case RenderState::RenderingAPI::Metal: impl = new FontStashMetal(); break;
  #else
    case RenderState::RenderingAPI::Metal: LOG(ERROR) << "Application was compiled without Metal support"; return nullptr;
  #endif
  #ifdef HAVE_OPENGL
    case RenderState::RenderingAPI::OpenGL: impl = new FontStashOpenGL(); break;
  #else
    case RenderState::RenderingAPI::OpenGL: LOG(ERROR) << "Application was compiled without OpenGL support"; return nullptr;
  #endif
    case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return nullptr;
  }
  
  impl->shader = shader;
  if (!impl->Initialize(textureWidth, textureHeight, renderState)) {
    delete impl;
    return nullptr;
  }
  
  return impl;
}

}
