#include "scan_studio/viewer_common/opengl/util.hpp"

namespace scan_studio {

Eigen::Matrix4f PerspectiveMatrixOpenGL(float verticalFov, float aspectRatio, float zNear, float zFar) {
  float const tanHalfFovy = tan(verticalFov / 2.f);
  
  Eigen::Matrix4f result;
  result.setZero();
  
  // X scaling
  result(0, 0) = 1.f / (aspectRatio * tanHalfFovy);
  
  // Y scaling
  result(1, 1) = -1.f / (tanHalfFovy);
  
  // This gets the resulting (X, Y, Z) divided by Z
  result(3, 2) = 1.f;
  
  // This sets the resulting Z such that it ranges from -1 to 1 after division by result-W = input-Z.
  result(2, 2) = (zFar + zNear) / (zFar - zNear);
  result(2, 3) = -(2 * zFar * zNear) / (zFar - zNear);
  
  return result;
}

string GetGLErrorName(GLenum error_code) {
  if (error_code == GL_NO_ERROR) {
    return "GL_NO_ERROR";
  } else if (error_code == GL_INVALID_ENUM) {
    return "GL_INVALID_ENUM";
  } else if (error_code == GL_INVALID_VALUE) {
    return "GL_INVALID_VALUE";
  } else if (error_code == GL_INVALID_OPERATION) {
    return "GL_INVALID_OPERATION";
  } else if (error_code == GL_INVALID_FRAMEBUFFER_OPERATION) {
    return "GL_INVALID_FRAMEBUFFER_OPERATION";
  } else if (error_code == GL_OUT_OF_MEMORY) {
    return "GL_OUT_OF_MEMORY";
  }/* else if (error_code == GL_STACK_UNDERFLOW) {
    return "GL_STACK_UNDERFLOW";
  } else if (error_code == GL_STACK_OVERFLOW) {
    return "GL_STACK_OVERFLOW";
  }*/ else {
    return "UNKNOWN GL ERROR";
  }
}

string GetGLErrorDescription(GLenum error_code) {
  if (error_code == GL_NO_ERROR) {
    return "No error has been recorded.";
  } else if (error_code == GL_INVALID_ENUM) {
    return "An unacceptable value is specified for an enumerated argument. The offending command is ignored and has no other side effect than to set the error flag.";
  } else if (error_code == GL_INVALID_VALUE) {
    return "A numeric argument is out of range. The offending command is ignored and has no other side effect than to set the error flag.";
  } else if (error_code == GL_INVALID_OPERATION) {
    return "The specified operation is not allowed in the current state. The offending command is ignored and has no other side effect than to set the error flag.";
  } else if (error_code == GL_INVALID_FRAMEBUFFER_OPERATION) {
    return "The framebuffer object is not complete. The offending command is ignored and has no other side effect than to set the error flag.";
  } else if (error_code == GL_OUT_OF_MEMORY) {
    return "There is not enough memory left to execute the command. The state of the GL is undefined, except for the state of the error flags, after this error is recorded.";
  }/* else if (error_code == GL_STACK_UNDERFLOW) {
    return " An attempt has been made to perform an operation that would cause an internal stack to underflow.";
  } else if (error_code == GL_STACK_OVERFLOW) {
    return "An attempt has been made to perform an operation that would cause an internal stack to overflow.";
  }*/ else {
    return "";
  }
}

}
