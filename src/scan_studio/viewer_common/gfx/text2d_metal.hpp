#pragma once
#ifdef __APPLE__

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/text2d.hpp"

#include "scan_studio/viewer_common/gfx/fontstash_metal_shader.h"

namespace scan_studio {
using namespace vis;

struct MetalRenderState;

class Text2DMetal : public Text2D {
 public:
  ~Text2DMetal();
  
  virtual bool Initialize(int viewCount, FontStash* fontStash, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) override;
  
  /// See enum FONSalign for possible values for align (e.g.: FONS_ALIGN_LEFT | FONS_ALIGN_BOTTOM)
  virtual void SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  virtual void SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  
  virtual void RenderView(int viewIndex, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) override;
  
 private:
  void RenderBatch(FONSVertex* vertices, int count, int textureIndex, int viewIndex);
  
  FONSText fonsText;
  
  Fontstash_Vertex_InstanceData vertexInstanceData;
  
  MetalRenderState* thisFramesRenderState = nullptr;
  
  FontStash* fontStash = nullptr;  // not owned
};

}
#endif
