#ifdef HAVE_OPENGL
#include "scan_studio/viewer_common/gfx/text2d_opengl.hpp"

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <loguru.hpp>

#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"

#include "scan_studio/viewer_common/gfx/fontstash.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_opengl.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

namespace scan_studio {

Text2DOpenGL::~Text2DOpenGL() {
  Destroy();
}

bool Text2DOpenGL::Initialize(int /*viewCount*/, FontStash* fontStash, RenderState* /*renderState*/) {
  this->fontStash = fontStash;
  return true;
}

void Text2DOpenGL::Destroy() {
  // Nothing yet since the class does not own graphic objects itself (they are created by fontstash instead)
}

void Text2DOpenGL::GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) {
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  float bounds[4];
  fonsTextBounds(context, x, y, text, nullptr, bounds);
  
  *xMin = bounds[0];
  *yMin = bounds[1];
  *xMax = bounds[2];
  *yMax = bounds[3];
}

void Text2DOpenGL::SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  fonsText.vertices.clear();
  fonsText.batches.clear();
  
  int textLen = strlen(text);
  
  if (textLen == 0) { return; }
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  u32 red = 255.f * color.x() + 0.5f;
  u32 green = 255.f * color.y() + 0.5f;
  u32 blue = 255.f * color.z() + 0.5f;
  u32 alpha = 255.f * color.w() + 0.5f;
  fonsSetColor(context, red | (green << 8) | (blue << 16) | (alpha << 24));
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  // fonsSetSpacing(context, float spacing);
  // fonsSetBlur(context, float blur);
  
  /*float newX =*/ fonsCreateText(context, x, y, text, text + textLen, &fonsText);
}

void Text2DOpenGL::SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  fonsText.vertices.clear();
  fonsText.batches.clear();
  
  int textLen = strlen(text);
  
  if (textLen == 0) { return; }
  if (fontStash == nullptr) { LOG(ERROR) << "Text2D object not initialized"; return; }
  FONScontext* context = fontStash->GetContext();
  
  u32 red = 255.f * color.x() + 0.5f;
  u32 green = 255.f * color.y() + 0.5f;
  u32 blue = 255.f * color.z() + 0.5f;
  u32 alpha = 255.f * color.w() + 0.5f;
  fonsSetColor(context, red | (green << 8) | (blue << 16) | (alpha << 24));
  fonsSetAlign(context, align);
  fonsSetFont(context, font);
  fonsSetSize(context, fontSize);
  
  // fonsSetSpacing(context, float spacing);
  // fonsSetBlur(context, float blur);
  
  /*float newX =*/ fonsCreateTextBox(context, minX, minY, maxX, maxY, /*wordWrap*/ true, text, text + textLen, &fonsText);
}

void Text2DOpenGL::RenderView(int /*viewIndex*/, RenderState* /*renderState*/) {
  // OLD:
  // fonsBatchText(fontStash->GetContext(), fonsText);
  // fonsFlushRender(fontStash->GetContext());
  
  // Loop over all batches in the given text
  int batchEndVertex = fonsText.vertices.size();
  
  for (int b = static_cast<int>(fonsText.batches.size()) - 1; b >= 0; -- b) {
    const auto& textBatch = fonsText.batches[b];
    
    // Render vertices [textBatch.firstVertex, batchEndVertex[
    RenderBatch(fonsText.vertices.data() + textBatch.firstVertex, batchEndVertex - textBatch.firstVertex, textBatch.textureId);
    
    batchEndVertex = textBatch.firstVertex;
  }
}

void Text2DOpenGL::SetModelViewProjection(int /*viewIndex*/, const float* columnMajorModelViewProjectionData) {
  memcpy(modelViewProjection, columnMajorModelViewProjectionData, 4 * 4 * sizeof(float));
  
  // Invert Y for OpenGL
  for (int i = 1; i < 16; i += 4) {
    modelViewProjection[i] = -1 * modelViewProjection[i];
  }
}

void Text2DOpenGL::RenderBatch(FONSVertex* vertices, int count, int textureIndex) {
  FontStashOpenGL* fontStashOpenGL = reinterpret_cast<FontStashOpenGL*>(fontStash);
  FontStashShaderOpenGL* shaderOpenGL = fontStashOpenGL->Shader()->GetOpenGLImpl();
  
  CHECK_OPENGL_NO_ERROR();
  const GLTexture* currentTexture = fontStashOpenGL->FindTextureForRendering(textureIndex);
  if (currentTexture == nullptr) { return; }
  
  gl.glDisable(GL_DEPTH_TEST);
  
  // Using premultiplied alpha to get alpha output to the render target, with good behavior in (bilinear) interpolation.
  // Alpha output is necessary for blending with the passthrough layer in the Quest viewer.
  // https://apoorvaj.io/alpha-compositing-opengl-blending-and-premultiplied-alpha/#premultiplied-alpha
  gl.glEnable(GL_BLEND);
  gl.glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  gl.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  
  shaderOpenGL->program->UseProgram();
  CHECK_OPENGL_NO_ERROR();
  
  // Configure attributes
  fontStashOpenGL->GeometryBuffer().BufferData(vertices, count * sizeof(FONSVertex), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
  u32 offset = 0;
  shaderOpenGL->program->SetPositionAttribute(2, GL_FLOAT, /*stride*/ sizeof(FONSVertex), offset);
  offset += 2 * sizeof(float);
  shaderOpenGL->program->SetTexCoordAttribute(2, GL_FLOAT, /*stride*/ sizeof(FONSVertex), offset);
  offset += 2 * sizeof(float);
  shaderOpenGL->program->SetColorAttribute(4, GL_UNSIGNED_BYTE, /*stride*/ sizeof(FONSVertex), offset);
  offset += 4;
  
  // Update uniforms
  gl.glActiveTexture(GL_TEXTURE0);
  gl.glBindTexture(GL_TEXTURE_2D, currentTexture->Name());
  gl.glUniform1i(shaderOpenGL->fontTextureLocation, 0);  // use GL_TEXTURE0
  gl.glUniformMatrix4fv(shaderOpenGL->modelViewProjectionLocation, /*count*/ 1, /*transpose*/ GL_FALSE, modelViewProjection);
  
  // Draw
  gl.glDrawArrays(GL_TRIANGLES, /*first*/ 0, count);
  
  // Reset states (not necessarily needed)
  shaderOpenGL->program->DisableColorAttribute();
  shaderOpenGL->program->DisableTexCoordAttribute();
  shaderOpenGL->program->DisablePositionAttribute();
  gl.glDisable(GL_BLEND);
  
  CHECK_OPENGL_NO_ERROR();
}

}

#endif
