#include "scan_studio/viewer_common/opengl/buffer.hpp"

namespace scan_studio {

GLBuffer::GLBuffer(GLBuffer&& other)
    : bufferName(other.bufferName),
      size(other.size),
      usage(other.usage),
      bufferIsAllocated(other.bufferIsAllocated),
      bufferIsWrapped(other.bufferIsWrapped) {
  other.bufferIsAllocated = false;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) {
  swap(bufferName, other.bufferName);
  swap(size, other.size);
  swap(usage, other.usage);
  swap(bufferIsAllocated, other.bufferIsAllocated);
  swap(bufferIsWrapped, other.bufferIsWrapped);
  
  return *this;
}

GLBuffer::~GLBuffer() {
  Destroy();
}

void GLBuffer::Destroy() {
  if (bufferIsAllocated && !bufferIsWrapped) {
    gl.glDeleteBuffers(1, &bufferName);
  }
  
  size = 0;
  bufferIsAllocated = false;
  bufferIsWrapped = false;
}

void GLBuffer::Allocate(vis::usize sizeInBytes, GLenum target, GLenum usage) {
  EnsureBufferIsAllocatedAndBound(target);
  
  this->usage = usage;
  size = sizeInBytes;
  gl.glBufferData(target, size, nullptr, usage);
}

void GLBuffer::Wrap(GLenum target, GLuint bufferName) {
  Destroy();
  
  this->bufferName = bufferName;
  
  gl.glBindBuffer(target, bufferName);
  GLint sizeGLint;
  gl.glGetBufferParameteriv(target, GL_BUFFER_SIZE, &sizeGLint);
  size = sizeGLint;
  
  bufferIsAllocated = true;
  bufferIsWrapped = true;
}

void GLBuffer::BufferData(const void* data, usize sizeInBytes, GLenum target, GLenum usage) {
  EnsureBufferIsAllocatedAndBound(target);
  
  this->usage = usage;
  size = sizeInBytes;
  gl.glBufferData(target, size, data, usage);
}

void GLBuffer::EnsureBufferIsAllocatedAndBound(GLenum target) {
  if (!bufferIsAllocated) {
    gl.glGenBuffers(1, &bufferName);
    bufferIsAllocated = true;
  }
  
  gl.glBindBuffer(target, bufferName);
}

}
