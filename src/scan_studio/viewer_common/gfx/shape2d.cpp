#include "scan_studio/viewer_common/gfx/shape2d.hpp"

#include "scan_studio/viewer_common/gfx/shape2d_opengl.hpp"
#ifdef HAVE_VULKAN
  #include "scan_studio/viewer_common/gfx/shape2d_vulkan.hpp"
#endif
#ifdef __APPLE__
  #include "scan_studio/viewer_common/gfx/shape2d_metal.hpp"
#endif

#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

Shape2D* Shape2D::Create(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) {
  Shape2D* impl = nullptr;
  
  switch (renderState->api) {
  #ifdef HAVE_VULKAN
    case RenderState::RenderingAPI::Vulkan: impl = new Shape2DVulkan(); break;
  #else
    case RenderState::RenderingAPI::Vulkan: LOG(ERROR) << "Application was compiled without Vulkan support"; return nullptr;
  #endif
  #ifdef __APPLE__
    case RenderState::RenderingAPI::Metal: impl = new Shape2DMetal(); break;
  #else
    case RenderState::RenderingAPI::Metal: LOG(ERROR) << "Application was compiled without Metal support"; return nullptr;
  #endif
  #ifdef HAVE_OPENGL
    case RenderState::RenderingAPI::OpenGL: impl = new Shape2DOpenGL(); break;
  #else
    case RenderState::RenderingAPI::OpenGL: LOG(ERROR) << "Application was compiled without OpenGL support"; return nullptr;
  #endif
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

bool Shape2D::Create(unique_ptr<Shape2D>* ptr, int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) {
  ptr->reset(Shape2D::Create(maxVertices, maxIndices, viewCount, shader, renderState));
  return ptr != nullptr;
}

}
