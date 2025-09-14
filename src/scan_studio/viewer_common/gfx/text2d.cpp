#include "scan_studio/viewer_common/gfx/text2d.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/text2d_opengl.hpp"
#ifdef HAVE_VULKAN
  #include "scan_studio/viewer_common/gfx/text2d_vulkan.hpp"
#endif
#ifdef __APPLE__
  #include "scan_studio/viewer_common/gfx/text2d_metal.hpp"
#endif

namespace scan_studio {

Text2D* Text2D::Create(int viewCount, FontStash* fontStash, RenderState* renderState) {
  Text2D* impl = nullptr;
  
  switch (renderState->api) {
  #ifdef HAVE_VULKAN
    case RenderState::RenderingAPI::Vulkan: impl = new Text2DVulkan(); break;
  #else
    case RenderState::RenderingAPI::Vulkan: LOG(ERROR) << "Application was compiled without Vulkan support"; return nullptr;
  #endif
  #ifdef __APPLE__
    case RenderState::RenderingAPI::Metal: impl = new Text2DMetal(); break;
  #else
    case RenderState::RenderingAPI::Metal: LOG(ERROR) << "Application was compiled without Metal support"; return nullptr;
  #endif
  #ifdef HAVE_OPENGL
    case RenderState::RenderingAPI::OpenGL: impl = new Text2DOpenGL(); break;
  #else
    case RenderState::RenderingAPI::OpenGL: LOG(ERROR) << "Application was compiled without OpenGL support"; return nullptr;
  #endif
    case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return nullptr;
  }
  
  impl->Initialize(viewCount, fontStash, renderState);
  
  return impl;
}

bool Text2D::Create(unique_ptr<Text2D>* ptr, int viewCount, FontStash* fontStash, RenderState* renderState) {
  ptr->reset(Text2D::Create(viewCount, fontStash, renderState));
  return ptr != nullptr;
}

}
