#include "scan_studio/viewer_common/opengl/shader.hpp"

#include <memory>

#include <loguru.hpp>

#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"

namespace scan_studio {

ShaderProgram::ShaderProgram()
    : program_(0),
      vertex_shader_(0),
      fragment_shader_(0),
      position_attribute_location_(-1),
      color_attribute_location_(-1) {}

ShaderProgram::~ShaderProgram() {
  if (vertex_shader_ != 0) {
    gl.glDetachShader(program_, vertex_shader_);
    gl.glDeleteShader(vertex_shader_);
  }
  
  if (fragment_shader_ != 0) {
    gl.glDetachShader(program_, fragment_shader_);
    gl.glDeleteShader(fragment_shader_);
  }

  if (program_ != 0) {
    gl.glDeleteProgram(program_);
  }
}

bool ShaderProgram::AttachShader(const char* source_code, ShaderType type) {
  CHECK(program_ == 0) << "Cannot attach a shader after linking the program.";
  
  GLenum shader_enum;
  GLuint* shader_name = nullptr;
  if (type == ShaderType::kVertexShader) {
    shader_enum = GL_VERTEX_SHADER;
    shader_name = &vertex_shader_;
  } else if (type == ShaderType::kFragmentShader) {
    shader_enum = GL_FRAGMENT_SHADER;
    shader_name = &fragment_shader_;
  } else {
    LOG(ERROR) << "Unknown shader type: " << static_cast<int>(type);
    return false;
  }
  
  *shader_name = gl.glCreateShader(shader_enum);
  const GLchar* source_code_ptr =
      static_cast<const GLchar*>(source_code);
  gl.glShaderSource(*shader_name, 1, &source_code_ptr, NULL);
  gl.glCompileShader(*shader_name);

  GLint compiled;
  gl.glGetShaderiv(*shader_name, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint length;
    gl.glGetShaderiv(*shader_name, GL_INFO_LOG_LENGTH, &length);
    unique_ptr<GLchar[]> log(
        reinterpret_cast<GLchar*>(new uint8_t[length]));
    gl.glGetShaderInfoLog(*shader_name, length, &length, log.get());
    LOG(ERROR) << "GL Shader Compilation Error: " << log.get();
    return false;
  }
  return true;
}

bool ShaderProgram::CreateProgram() {
  if (program_ != 0) {
    LOG(ERROR) << "Program already created.";
    return true;
  }
  
  // TODO: Error-check the function calls below
  program_ = gl.glCreateProgram();
  if (fragment_shader_ != 0) {
    gl.glAttachShader(program_, fragment_shader_);
  }
  if (vertex_shader_ != 0) {
    gl.glAttachShader(program_, vertex_shader_);
  }
  
  return true;
}

bool ShaderProgram::LinkProgram() {
  if (program_ == 0) {
    if (!CreateProgram()) {
      return false;
    }
  }
  
  gl.glLinkProgram(program_);
  
  GLint linked;
  gl.glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint length;
    gl.glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
    unique_ptr<GLchar[]> log(
        reinterpret_cast<GLchar*>(new uint8_t[length]));
    gl.glGetProgramInfoLog(program_, length, &length, log.get());
    LOG(ERROR) << "GL Program Linker Error: " << log.get();
    return false;
  }
  
  // Get attributes.
  position_attribute_location_ = gl.glGetAttribLocation(program_, "in_position");
  color_attribute_location_ = gl.glGetAttribLocation(program_, "in_color");
  texcoord_attribute_location_ = gl.glGetAttribLocation(program_, "in_texcoord");
  return true;
}

void ShaderProgram::UseProgram() const {
  gl.glUseProgram(program_);
}

GLint ShaderProgram::GetUniformLocation(const char* name) const {
  return gl.glGetUniformLocation(program_, name);
}

GLint ShaderProgram::GetUniformLocationOrAbort(const char* name) const {
  GLint result = gl.glGetUniformLocation(program_, name);
  if (result == -1) {
    LOG(WARNING) << "Uniform does not exist (might have been optimized out by the compiler): " << name;
  }
  return result;
}

void ShaderProgram::SetUniformMatrix2fv(GLint location, float* values, bool valuesAreColumnMajor) {
  gl.glUniformMatrix2fv(location, 1, valuesAreColumnMajor ? GL_FALSE : GL_TRUE, values);
}

void ShaderProgram::SetPositionAttribute(int component_count, GLenum component_type, GLsizei stride, size_t offset) {
  // CHECK(position_attribute_location_ != -1) << "SetPositionAttribute() called, but no attribute \"in_position\" found.";
  if (position_attribute_location_ == -1) {
    // Allow using an object with positions with a material that ignores the positions.
    return;
  }
  
  gl.glEnableVertexAttribArray(position_attribute_location_);
  gl.glVertexAttribPointer(
      position_attribute_location_,
      component_count,
      component_type,
      GL_FALSE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}

void ShaderProgram::DisablePositionAttribute() {
  if (position_attribute_location_ == -1) {
    // Allow using an object with positions with a material that ignores the positions.
    return;
  }
  
  gl.glDisableVertexAttribArray(position_attribute_location_);
}

void ShaderProgram::SetColorAttribute(int component_count, GLenum component_type, GLsizei stride, size_t offset) {
  // CHECK(color_attribute_location_ != -1) << "SetColorAttribute() called, but no attribute \"in_color\" found.";
  if (color_attribute_location_ == -1) {
    // Allow using an object with colors with a material that ignores the colors.
    return;
  }
  
  gl.glEnableVertexAttribArray(color_attribute_location_);
  gl.glVertexAttribPointer(
      color_attribute_location_,
      component_count,
      component_type,
      GL_TRUE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}

void ShaderProgram::DisableColorAttribute() {
  if (color_attribute_location_ == -1) {
    // Allow using an object with colors with a material that ignores the colors.
    return;
  }
  
  gl.glDisableVertexAttribArray(color_attribute_location_);
}

void ShaderProgram::SetTexCoordAttribute(int component_count, GLenum component_type, GLsizei stride, size_t offset) {
  // CHECK(texcoord_attribute_location_ != -1) << "SetTexCoordAttribute() called, but no attribute \"in_texcoord\" found.";
  if (texcoord_attribute_location_ == -1) {
    // Allow using an object with texcoords with a material that ignores the texcoords.
    return;
  }
  
  gl.glEnableVertexAttribArray(texcoord_attribute_location_);
  gl.glVertexAttribPointer(
      texcoord_attribute_location_,
      component_count,
      component_type,
      GL_FALSE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}

void ShaderProgram::DisableTexCoordAttribute() {
  if (texcoord_attribute_location_ == -1) {
    // Allow using an object with texcoords with a material that ignores the texcoords.
    return;
  }
  
  gl.glDisableVertexAttribArray(texcoord_attribute_location_);
}

}
