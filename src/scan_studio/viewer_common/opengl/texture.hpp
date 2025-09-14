#pragma once

#include <vector>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <loguru.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/loader.hpp"

namespace scan_studio {
using namespace vis;

/// Helper function to choose a suitable size of a 2D texture (with power-of-two width) that has the given minimum number of pixels and
/// adheres to the texture size limitations while wasting the minimum amount of memory. Exception: If a 1D texture may be allocated with
/// the exact number of required pixels as width (i.e., minTexelCount <= maxTextureSize), then the size (minTexelCount, 1) is returned,
/// even if minTexelCount is not a power of two, or it is smaller than minTextureWidth.
///
/// minTextureWidth must be a power-of-two value.
/// maxTextureSize can be set to the value returned for GL_MAX_TEXTURE_SIZE (which must be at least 2048 in OpenGL ES 3.0).
/// If no suitable texture size is found, *chosenWidth and *chosenHeight are set to numeric_limits<u32>::max().
void ChooseTextureSizeForTexelCount(u32 minTexelCount, u32 minTextureWidth, u32 maxTextureSize, u32* chosenWidth, u32* chosenHeight);

/// Wraps an OpenGL texture object.
class GLTexture {
 public:
  inline GLTexture() {}
  
  GLTexture(GLTexture&& other);
  GLTexture& operator= (GLTexture&& other);
  
  GLTexture(const GLTexture& other) = delete;
  GLTexture& operator= (const GLTexture& other) = delete;
  
  /// Destructor, destroys the texture in case it has been allocated.
  ~GLTexture();
  
  /// Deletes the texture memory (if the texture was allocated).
  void Destroy();
  
  /// Allocates a 2D texture. Optionally, fills the texture with the provided data.
  /// This function may also be used to re-allocate an existing texture.
  bool Allocate2D(
      int width, int height,
      GLint internalFormat, GLenum format, GLenum type,
      GLint wrapModeX, GLint wrapModeY,
      GLint filterModeMag, GLint filterModeMin,
      const void* data = nullptr);
  
  /// Wraps an externally allocated texture.
  /// Will not destroy the texture when deallocated: The user remains responsible for keeping the external
  /// texture alive while the GLTexture is in use, and is responsible for destroying it afterwards.
  void Wrap(GLuint textureName, int width, int height);
  
  /// Returns the OpenGL texture name. This is only valid after the texture has been allocated.
  inline GLuint Name() const { return name; }
  
  inline int Width() const { return width; }
  inline int Height() const { return height; }
  
  inline bool IsAllocated() const { return textureIsAllocated; }
  
 private:
  /// Texture name in OpenGL
  GLuint name;
  
  /// Whether the texture has been allocated.
  bool textureIsAllocated = false;
  
  /// Whether the texture is external and won't be destroyed together with this GLBuffer object.
  bool textureIsWrapped = false;
  
  /// Texture dimensions cached for convenience
  int width = 0;
  int height = 0;
};

}
