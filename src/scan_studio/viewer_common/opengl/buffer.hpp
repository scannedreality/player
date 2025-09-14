#pragma once

#include <vector>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <loguru.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/opengl/extensions.hpp"
#include "scan_studio/viewer_common/opengl/loader.hpp"

namespace scan_studio {
using namespace vis;

/// Wraps an OpenGL buffer object, which can for example store vertex or index data.
class GLBuffer {
 public:
  inline GLBuffer() {}
  
  GLBuffer(GLBuffer&& other);
  GLBuffer& operator= (GLBuffer&& other);
  
  GLBuffer(const GLBuffer& other) = delete;
  GLBuffer& operator= (const GLBuffer& other) = delete;
  
  /// Destructor, destroys the buffer in case it has been allocated.
  ~GLBuffer();
  
  /// Deletes the buffer memory (if the buffer was allocated).
  void Destroy();
  
  /// Allocates the buffer, but does not initialize the buffer contents.
  void Allocate(usize sizeInBytes, GLenum target, GLenum usage);
  
  /// Wraps an externally allocated buffer.
  /// Will not destroy the buffer when deallocated: The user remains responsible for keeping the external
  /// buffer alive while the GLBuffer is in use, and is responsible for destroying it afterwards.
  void Wrap(GLenum target, GLuint bufferName);
  
  /// Transfers the given data to the buffer. This allocates the buffer if necessary, and resizes the buffer to the data size.
  ///
  /// @param data        The vector to transfer.
  /// @param sizeInBytes The size of the data in bytes.
  /// @param target      The OpenGL target to bind the buffer to, for example, GL_ARRAY_BUFFER.
  /// @param usage       The OpenGL usage hint to use for the buffer, for example, GL_STATIC_DRAW, GL_DYNAMIC_DRAW, or GL_STREAM_DRAW.
  void BufferData(const void* data, usize sizeInBytes, GLenum target, GLenum usage);
  
  /// Transfers the given vector to the buffer. This allocates the buffer if necessary, and resizes the buffer to the vector's size.
  ///
  /// @param data   The vector to transfer.
  /// @param target The OpenGL target to bind the buffer to, for example, GL_ARRAY_BUFFER.
  /// @param usage  The OpenGL usage hint to use for the buffer, for example, GL_STATIC_DRAW, GL_DYNAMIC_DRAW, or GL_STREAM_DRAW.
  template <typename T>
  void BufferData(const vector<T>& data, GLenum target, GLenum usage) {
    EnsureBufferIsAllocatedAndBound(target);
    
    this->usage = usage;
    size = data.size() * sizeof(T);
    gl.glBufferData(target, size, data.data(), usage);
  }
  
  /// Returns the OpenGL buffer name. This is only valid after the buffer has been allocated
  /// by a call to BufferData().
  inline GLuint BufferName() const { return bufferName; }
  
  /// Returns the size of the buffer in bytes.
  inline usize Size() const { return size; }
  
 private:
  /// Allocates the buffer *name* (not its contents) in case it is not allocated yet and binds the buffer.
  void EnsureBufferIsAllocatedAndBound(GLenum target);
  
  /// The OpenGL buffer name.
  GLuint bufferName;
  
  /// The buffer's size in bytes.
  usize size = 0;
  
  /// The buffer's usage mode.
  GLenum usage = 0;
  
  /// Whether the buffer has been allocated.
  bool bufferIsAllocated = false;
  
  /// Whether the buffer is external and won't be destroyed together with this GLBuffer object.
  bool bufferIsWrapped = false;
};

}
