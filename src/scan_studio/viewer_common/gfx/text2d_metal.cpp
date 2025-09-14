#ifdef __APPLE__

#include "scan_studio/viewer_common/gfx/text2d_metal.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/gfx/fontstash.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_metal.hpp"

namespace scan_studio {

Text2DMetal::~Text2DMetal() {
  Destroy();
}

bool Text2DMetal::Initialize(int /*viewCount*/, FontStash* fontStash, RenderState* /*renderState*/) {
  this->fontStash = fontStash;
  
  return true;
}

void Text2DMetal::Destroy() {}

void Text2DMetal::GetTextBounds(float x, float y, int align, const char* text, int font, float fontSize, float* xMin, float* yMin, float* xMax, float* yMax) {
  // TODO: This function is duplicated among the different render paths. De-duplicate the code.
  
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

void Text2DMetal::SetText(float x, float y, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  // TODO: This function is duplicated among the different render paths. De-duplicate the code.
  
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

void Text2DMetal::SetTextBox(float minX, float minY, float maxX, float maxY, int align, const char* text, const Eigen::Vector4f& color, int font, float fontSize, RenderState* /*renderState*/) {
  // TODO: This function is duplicated among the different render paths. De-duplicate the code.
  
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

void Text2DMetal::RenderView(int viewIndex, RenderState* renderState) {
  thisFramesRenderState = renderState->AsMetalRenderState();
  
  // OLD:
  // fonsBatchText(fontStash->GetContext(), fonsText);
  // fonsFlushRender(fontStash->GetContext());
  
  // Loop over all batches in the given text
  int batchEndVertex = fonsText.vertices.size();
  
  for (int b = static_cast<int>(fonsText.batches.size()) - 1; b >= 0; -- b) {
    const auto& textBatch = fonsText.batches[b];
    
    // Render vertices [textBatch.firstVertex, batchEndVertex[
    RenderBatch(fonsText.vertices.data() + textBatch.firstVertex, batchEndVertex - textBatch.firstVertex, textBatch.textureId, viewIndex);
    
    batchEndVertex = textBatch.firstVertex;
  }
}

void Text2DMetal::SetModelViewProjection(int /*viewIndex*/, const float* columnMajorModelViewProjectionData) {
  memcpy(&vertexInstanceData.modelViewProjection, columnMajorModelViewProjectionData, sizeof(vertexInstanceData.modelViewProjection));
}

void Text2DMetal::RenderBatch(FONSVertex* vertices, int count, int textureIndex, int /*viewIndex*/) {
  FontStashMetal* fontStashMetal = reinterpret_cast<FontStashMetal*>(fontStash);
  FontStashShaderMetal* shaderMetal = fontStash->Shader()->GetMetalImpl();
  MetalRenderState* metalState = thisFramesRenderState->AsMetalRenderState();
  auto& encoder = metalState->renderCmdEncoder;
  
  MTL::Texture* texture = fontStashMetal->FindTextureForRendering(textureIndex);
  if (texture == nullptr) { return; }
  
  MTL::Buffer* verticesBuffer;
  u64 verticesOffset;
  if (!fontStashMetal->BufferVertices(vertices, count, &verticesBuffer, &verticesOffset)) { return; }
  
  encoder->setCullMode(MTL::CullModeNone);
  encoder->setDepthStencilState(shaderMetal->depthStencilState.get());
  
  encoder->setRenderPipelineState(shaderMetal->renderPipelineState.get());
  encoder->setVertexBuffer(verticesBuffer, /*offset*/ 0, Fontstash_VertexInputIndex_vertices);
  encoder->setVertexBytes(&vertexInstanceData, sizeof(vertexInstanceData), Fontstash_VertexInputIndex_instanceData);
  encoder->setFragmentTexture(texture, Fontstash_FragmentTextureInputIndex_fontTexture);
  encoder->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, /*vertexStart*/ static_cast<NS::UInteger>(verticesOffset / sizeof(FONSVertex)), count);
}

}

#endif
