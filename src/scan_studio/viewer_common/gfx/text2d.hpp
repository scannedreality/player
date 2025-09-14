#pragma once

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"

namespace scan_studio {
using namespace vis;

class FontStash;
struct RenderState;
class Text2DShader;

/// Represents a homogeneously colored 2D text. Color and text can be updated per frame.
class Text2D {
 public:
  virtual inline ~Text2D() {}
  
  virtual bool Initialize(int viewCount, FontStash* fontStash, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
  
  virtual void GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) = 0;
  
  /// See enum FONSalign for possible values for align (e.g.: FONS_ALIGN_LEFT | FONS_ALIGN_BOTTOM)
  virtual void SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) = 0;
  virtual void SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* renderState) = 0;
  
  virtual void RenderView(int viewIndex, RenderState* renderState) = 0;
  
  virtual void SetModelViewProjection(int viewIndex, const float *columnMajorModelViewProjectionData) = 0;
  
  static Text2D* Create(int viewCount, FontStash* fontStash, RenderState* renderState);
  static bool Create(unique_ptr<Text2D>* ptr, int viewCount, FontStash* fontStash, RenderState* renderState);
};

}
