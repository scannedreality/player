#include "scan_studio/viewer_common/gfx/textured_shape2d.hpp"

// #include "scan_studio/viewer_common/gfx/textured_shape2d_opengl.hpp"
#ifdef HAVE_VULKAN
  #include "scan_studio/viewer_common/gfx/textured_shape2d_vulkan.hpp"
#endif

#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

TexturedShape2D* TexturedShape2D::Create(int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState) {
  TexturedShape2D* impl = nullptr;
  
  switch (renderState->api) {
  #ifdef HAVE_VULKAN
    case RenderState::RenderingAPI::Vulkan: impl = new TexturedShape2DVulkan(); break;
  #else
    case RenderState::RenderingAPI::Vulkan: LOG(ERROR) << "Application was compiled without Vulkan support"; return nullptr;
  #endif
  case RenderState::RenderingAPI::Metal: LOG(ERROR) << "TexturedShape2D is not implemented for the Metal render path yet"; return nullptr;
  case RenderState::RenderingAPI::OpenGL: LOG(FATAL) << "TexturedShape2DOpenGL is not implemented yet"; /*impl = new TexturedShape2DOpenGL();*/ break;
  case RenderState::RenderingAPI::D3D11: LOG(ERROR) << "TODO: This is not supported in the D3D11 render path yet"; return nullptr;
  }
  
  if (!impl->Initialize(maxVertices, maxIndices, viewCount, shader, renderState)) {
    delete impl;
    return nullptr;
  }
  
  impl->maxVertices = maxVertices;
  impl->maxIndices = maxIndices;
  
  return impl;
}

bool TexturedShape2D::Create(unique_ptr<TexturedShape2D>* ptr, int maxVertices, int maxIndices, int viewCount, TexturedShape2DShader* shader, RenderState* renderState) {
  ptr->reset(TexturedShape2D::Create(maxVertices, maxIndices, viewCount, shader, renderState));
  return ptr != nullptr;
}

}
