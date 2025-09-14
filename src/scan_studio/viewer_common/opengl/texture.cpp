#include "scan_studio/viewer_common/opengl/texture.hpp"

#include "scan_studio/viewer_common/opengl/util.hpp"

namespace scan_studio {

void ChooseTextureSizeForTexelCount(u32 minTexelCount, u32 minTextureWidth, u32 maxTextureSize, u32* chosenWidth, u32* chosenHeight) {
  if (minTexelCount <= maxTextureSize) {
    *chosenWidth = minTexelCount;
    *chosenHeight = 1;
    return;
  }
  
  *chosenWidth = numeric_limits<u32>::max();
  *chosenHeight = numeric_limits<u32>::max();
  
  u32 width = minTextureWidth;
  while (width <= maxTextureSize) {
    u32 height = (minTexelCount + width - 1) / width;
    
    if (height <= maxTextureSize &&
        ((*chosenWidth == numeric_limits<u32>::max()) || (width * height < *chosenWidth * *chosenHeight))) {
      *chosenWidth = width;
      *chosenHeight = height;
    }
    
    width *= 2;
  }
}


GLTexture::GLTexture(GLTexture&& other)
    : name(other.name),
      textureIsAllocated(other.textureIsAllocated),
      textureIsWrapped(other.textureIsWrapped),
      width(other.width),
      height(other.height) {
  other.textureIsAllocated = false;
}

GLTexture& GLTexture::operator=(GLTexture&& other) {
  swap(name, other.name);
  swap(textureIsAllocated, other.textureIsAllocated);
  swap(textureIsWrapped, other.textureIsWrapped);
  swap(width, other.width);
  swap(height, other.height);
  
  return *this;
}

GLTexture::~GLTexture() {
  Destroy();
}

void GLTexture::Destroy() {
  if (textureIsAllocated && !textureIsWrapped) {
    gl.glDeleteTextures(1, &name);
  }
  
  width = 0;
  height = 0;
  textureIsAllocated = false;
  textureIsWrapped = false;
}

bool GLTexture::Allocate2D(
    int width, int height,
    GLint internalFormat, GLenum format, GLenum type,
    GLint wrapModeX, GLint wrapModeY,
    GLint filterModeMag, GLint filterModeMin,
    const void* data) {
  // TODO: Error checking: return false if texture allocation failed
  
  const bool useTexSubImage =
      textureIsAllocated &&
      this->width == width &&
      this->height == height &&
      data != nullptr;
  
  this->width = width;
  this->height = height;
  
  if (!textureIsAllocated) {
    gl.glGenTextures(1, &name);
    textureIsAllocated = true;
  }
  
  gl.glBindTexture(GL_TEXTURE_2D, name);
  
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModeX);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModeY);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterModeMag);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterModeMin);
  
  gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  CHECK_OPENGL_NO_ERROR();
  
  if (useTexSubImage) {
    gl.glTexSubImage2D(
        GL_TEXTURE_2D,
        /*level*/ 0, /*xoffset*/ 0, /*yoffset*/ 0,
        width, height,
        format, type,
        data);
  } else {
    gl.glTexImage2D(
        GL_TEXTURE_2D,
        /*level*/ 0, internalFormat,
        width, height,
        /*border*/ 0, format, type,
        data);
  }
  
  CHECK_OPENGL_NO_ERROR();
  
  return true;
}

void GLTexture::Wrap(GLuint textureName, int width, int height) {
  Destroy();
  
  name = textureName;
  this->width = width;
  this->height = height;
  
  textureIsAllocated = true;
  textureIsWrapped = true;
}

}
