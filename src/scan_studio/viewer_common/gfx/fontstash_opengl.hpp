// This is an ALTERED version based on original code with the following license:
// -----------------------------------------------------------------------------
//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//	claim that you wrote the original software. If you use this software
//	in a product, an acknowledgment in the product documentation would be
//	appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//	misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// -----------------------------------------------------------------------------

#pragma once
#ifdef HAVE_OPENGL

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/buffer.hpp"
#include "scan_studio/viewer_common/opengl/texture.hpp"

#include "scan_studio/viewer_common/gfx/fontstash.hpp"

typedef struct FONScontext FONScontext;

namespace scan_studio {
using namespace vis;

struct FONSVertex;
class FontStashShader;

class FontStashOpenGL : public FontStash {
 public:
  ~FontStashOpenGL();
  
  /// See enum FONSflags for `flags`.
  virtual bool Initialize(int textureWidth, int textureHeight, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  /// Returns the assigned ID of the loaded font, or FONS_INVALID in case of an error.
  virtual int LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) override;
  
  virtual inline FONScontext* GetContext() const override { return context; }
  
  virtual void PrepareFrame(RenderState* renderState) override;
  
  const GLTexture* FindTextureForRendering(int textureIndex);
  inline GLBuffer& GeometryBuffer() { return geometryBuffer; }
  
 private:
  // FontStash callbacks
  int RenderCreate(int width, int height, int textureIndex);
  int RenderResize(int width, int height, int textureIndex);
  void RenderUpdate(int* rect, const unsigned char* data);
  void RenderDelete();
  void ErrorCallback(int error, int val);
  
  static int RenderCreate(void* userPtr, int width, int height, int textureIndex);
  static int RenderResize(void* userPtr, int width, int height, int textureIndex);
  static void RenderUpdate(void* userPtr, int* rect, const unsigned char* data);
  static void RenderDelete(void* userPtr);
  static void ErrorCallback(void* userPtr, int error, int val);
  
  int width;
  int height;
  /// Pairs of (textureIndex, texture)
  vector<pair<int, GLTexture>> textures;
  
  GLBuffer geometryBuffer;
  
  FONScontext* context = nullptr;  // owned object
};

}

#endif
