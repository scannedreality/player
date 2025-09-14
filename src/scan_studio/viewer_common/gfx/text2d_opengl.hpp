#pragma once
#ifdef HAVE_OPENGL

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/text2d.hpp"

namespace scan_studio {
using namespace vis;

class Text2DOpenGL : public Text2D {
 public:
  ~Text2DOpenGL();
  
  virtual bool Initialize(int viewCount, FontStash* fontStash, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  virtual void GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) override;
  
  /// See enum FONSalign for possible values for align (e.g.: FONS_ALIGN_LEFT | FONS_ALIGN_BOTTOM)
  virtual void SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  virtual void SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) override;
  
  virtual void RenderView(int viewIndex, RenderState* renderState) override;
  
  virtual void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) override;
  
 private:
  void RenderBatch(FONSVertex* vertices, int count, int textureIndex);
  
  FONSText fonsText;
  float modelViewProjection[16];
  
  FontStash* fontStash = nullptr;  // not owned
};

}

#endif
