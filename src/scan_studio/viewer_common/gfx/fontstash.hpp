#pragma once

#include <memory>

#include <libvis/vulkan/libvis.h>

namespace vis {
class InputStream;
}

namespace scan_studio {
using namespace vis;

struct FONScontext;
class FontStashShader;
struct RenderState;

class FontStash {
 public:
  virtual inline ~FontStash() {}
  
  /// See enum FONSflags for `flags`.
  virtual bool Initialize(int textureWidth, int textureHeight, RenderState* renderState) = 0;
  
  virtual void Destroy() = 0;
  
  /// Returns the assigned ID of the loaded font, or FONS_INVALID in case of an error.
  virtual int LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) = 0;
  
  virtual FONScontext* GetContext() const = 0;
  
  virtual void PrepareFrame(RenderState* renderState) = 0;
  
  /// Factory function, returning the implementation for the correct rendering API
  static FontStash* Create(int textureWidth, int textureHeight, FontStashShader* shader, RenderState* renderState);
  
  inline FontStashShader* Shader() const { return shader; }
  
 protected:
  FontStashShader* shader = nullptr;  // not owned
};

}
