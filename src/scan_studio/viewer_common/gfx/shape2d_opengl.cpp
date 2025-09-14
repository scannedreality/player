#ifdef HAVE_OPENGL
#include "scan_studio/viewer_common/gfx/shape2d_opengl.hpp"

#include "scan_studio/viewer_common/gfx/shape2d_shader.hpp"

#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"

namespace scan_studio {

Shape2DOpenGL::~Shape2DOpenGL() {
  Destroy();
}

bool Shape2DOpenGL::Initialize(int maxVertices, int maxIndices, int /*viewCount*/, Shape2DShader* /*shader*/, RenderState* /*renderState*/) {
  vertexBuffer.Allocate(maxVertices * 2 * sizeof(float), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
  indexBuffer.Allocate(maxIndices * sizeof(u16), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
  return true;
}

void Shape2DOpenGL::Destroy() {
  vertexBuffer.Destroy();
  indexBuffer.Destroy();
}

void Shape2DOpenGL::SetGeometry(int vertexCount, const Eigen::Vector2f* vertices, int indexCount, const u16* indices, RenderState* /*renderState*/) {
  this->indexCount = indexCount;
  
  vertexBuffer.BufferData(vertices, vertexCount * 2 * sizeof(float), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
  indexBuffer.BufferData(indices, indexCount * sizeof(u16), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
}

void Shape2DOpenGL::RenderView(const Eigen::Vector4f& color, int /*viewIndex*/, Shape2DShader* shader, RenderState* /*renderState*/) {
  Shape2DShaderOpenGL* shaderOpenGL = shader->GetOpenGLImpl();
  
  if (shader->DepthTestingEnabled()) {
    gl.glEnable(GL_DEPTH_TEST);
  } else {
    gl.glDisable(GL_DEPTH_TEST);
  }
  
  // Using premultiplied alpha to get alpha output to the render target, with good behavior in (bilinear) interpolation.
  // Alpha output is necessary for blending with the passthrough layer in the Quest viewer.
  // https://apoorvaj.io/alpha-compositing-opengl-blending-and-premultiplied-alpha/#premultiplied-alpha
  gl.glEnable(GL_BLEND);
  gl.glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  gl.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  
  // Use program
  shaderOpenGL->program->UseProgram();
  CHECK_OPENGL_NO_ERROR();
  
  // Configure attributes
  gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer.BufferName());
  CHECK_OPENGL_NO_ERROR();
  usize offset = 0;
  shaderOpenGL->program->SetPositionAttribute(2, GL_FLOAT, 2 * sizeof(float), offset);
  offset += 2 * sizeof(float);
  CHECK_OPENGL_NO_ERROR();
  
  // Update uniforms
  gl.glUniformMatrix4fv(shaderOpenGL->modelViewProjectionLocation, /*count*/ 1, /*transpose*/ GL_FALSE, modelViewProjection);
  gl.glUniform4f(shaderOpenGL->colorLocation, color(0), color(1), color(2), color(3));
  CHECK_OPENGL_NO_ERROR();
  
  // Draw
  gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer.BufferName());
  gl.glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, /*indices*/ nullptr);
  CHECK_OPENGL_NO_ERROR();
  
  // Reset states (not necessarily needed)
  shaderOpenGL->program->DisablePositionAttribute();
  gl.glDisable(GL_BLEND);
}

void Shape2DOpenGL::SetModelViewProjection(int /*viewIndex*/, const float* columnMajorModelViewProjectionData) {
  memcpy(modelViewProjection, columnMajorModelViewProjectionData, 4 * 4 * sizeof(float));
  
  // Invert Y for OpenGL
  for (int i = 1; i < 16; i += 4) {
    modelViewProjection[i] = -1 * modelViewProjection[i];
  }
}

}

#endif
