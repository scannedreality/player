#pragma once
#ifdef __APPLE__

#include <vector>

#include <Metal/Metal.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/shape2d.hpp"

#include "scan_studio/viewer_common/gfx/shape2d_metal_shader.h"

namespace scan_studio {
using namespace vis;

struct RenderState;

class Shape2DMetal : public Shape2D {
 public:
  ~Shape2DMetal();
  
  virtual bool Initialize(int maxVertices, int maxIndices, int viewCount, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* renderState) override;
  
  virtual void RenderView(const Eigen::Vector4f& color, int viewIndex, Shape2DShader* shader, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) override;
  
 private:
  // TODO: It is not necessarily best to have a managed buffer for each view and each frame-in-flight.
  //       The Vulkan render path uses only a single GPU-side buffer, and many staging buffers, for example.
  // TODO: Actually, the Metal best practices document recommends to directly do set<Vertex/Fragment>Bytes()
  //       for buffers up to 4K bytes, which is all buffers that we ever use with Shape2D. We should
  //       either use this exclusively, or use the current MTL::Buffer path only as a fallback for very large shapes.
  vector<NS::SharedPtr<MTL::Buffer>> vertexBuffers;
  vector<NS::SharedPtr<MTL::Buffer>> indexBuffers;
  
  Shape2D_Vertex_InstanceData vertexInstanceData;
  Shape2D_Fragment_InstanceData fragmentInstanceData;
  
  int indexCount;
  int currentPerFrameResourceIndex = 0;
  bool deviceHasUnifiedMemory;
};

}
#endif
