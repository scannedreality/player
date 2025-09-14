#ifdef HAVE_OPENGL
#include "scan_studio/viewer_common/gfx/fontstash_opengl.hpp"

#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

#include <loguru.hpp>

#include <libvis/io/input_stream.h>

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"

#include "scan_studio/viewer_common/opengl/loader.hpp"
#include "scan_studio/viewer_common/opengl/util.hpp"

#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

namespace scan_studio {

FontStashOpenGL::~FontStashOpenGL() {
  Destroy();
}

bool FontStashOpenGL::Initialize(int textureWidth, int textureHeight, RenderState* /*renderState*/) {
	FONSparams params;
  
	memset(&params, 0, sizeof(params));
	params.width = textureWidth;
	params.height = textureHeight;
	params.flags = FONS_ZERO_TOPLEFT;
	params.renderCreate = &FontStashOpenGL::RenderCreate;
	params.renderResize = &FontStashOpenGL::RenderResize;
	params.renderUpdate = &FontStashOpenGL::RenderUpdate;
	params.renderDelete = &FontStashOpenGL::RenderDelete;
	params.userPtr = this;
  
	context = fonsCreateInternal(&params);
	if (context == nullptr) { return false; }
	
	fonsSetErrorCallback(context, &FontStashOpenGL::ErrorCallback, this);
	
	return true;
}

void FontStashOpenGL::Destroy() {
  if (context) {
    fonsDeleteInternal(context);
    context = nullptr;
  }
}

int FontStashOpenGL::LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) {
  // TODO: We should share the file loading code with FontStashVulkan, it is identical
  if (context == nullptr) { LOG(ERROR) << "Initialize() has not been called yet"; return false; }
  
  vector<u8> data;
  if (ttfFileStream == nullptr) { LOG(ERROR) << "Tried to load a null file"; return false; }
  if (!ttfFileStream->ReadAll(&data)) { return false; }
  
  return fonsAddFontMem(context, name, std::move(data));
}

void FontStashOpenGL::PrepareFrame(RenderState* /*renderState*/) {
  // Drop all textures except for the current one
  if (textures.size() > 1) {
    textures.erase(textures.begin(), textures.begin() + (textures.size() - 1));
  }
}

const GLTexture* FontStashOpenGL::FindTextureForRendering(int textureIndex) {
  const GLTexture* currentTexture = nullptr;
  for (const auto& texture : textures) {
    if (texture.first == textureIndex) {
      currentTexture = &texture.second;
      break;
    }
  }
  if (currentTexture == nullptr) {
    LOG(ERROR) << "Could not find textureIndex " << textureIndex;
    return nullptr;
  }
  return currentTexture;
}

int FontStashOpenGL::RenderCreate(int width, int height, int textureIndex) {
  CHECK_OPENGL_NO_ERROR();
  
  const void* initialData = nullptr;
  
  // Firefox prefers to have textures initialized with something before glTexSubImage2D() is called on them (in RenderUpdate()).
  #ifdef __EMSCRIPTEN__
    vector<u8> zeroes(width * height);
    memset(zeroes.data(), 0, zeroes.size());
    initialData = zeroes.data();
  #endif
  
  textures.emplace_back(make_pair(textureIndex, GLTexture()));
  textures.back().second.Allocate2D(
      width, height,
      /*GL_R8*/ 0x8229, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE,
      GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
      GL_LINEAR, GL_LINEAR,
      initialData);
  
  this->width = width;
  this->height = height;
  
  CHECK_OPENGL_NO_ERROR();
  return 1;
}

int FontStashOpenGL::RenderResize(int width, int height, int textureIndex) {
  return RenderCreate(width, height, textureIndex);
}

void FontStashOpenGL::RenderUpdate(int* rect, const unsigned char* data) {
  CHECK_OPENGL_NO_ERROR();
  if (textures.empty()) { return; }
  
  const int updateWidth = rect[2] - rect[0];
  const int updateHeight = rect[3] - rect[1];
  
  gl.glBindTexture(GL_TEXTURE_2D, textures.back().second.Name());
  
  gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  
  // if (gl.glBeginTransformFeedback != nullptr) {
    // OpenGL ES 3.0 render path
    gl.glPixelStorei(/*GL_UNPACK_ROW_LENGTH*/ 0x0CF2, width);
    gl.glTexSubImage2D(GL_TEXTURE_2D, /*level*/ 0, rect[0], rect[1], updateWidth, updateHeight, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE, data + rect[0] + rect[1] * width);
    gl.glPixelStorei(/*GL_UNPACK_ROW_LENGTH*/ 0x0CF2, 0);  // reset to default value
  // } else {
  //   // OpenGL ES 2.0 render path
  //   // NOTE: GLES 2.0 does not have GL_UNPACK_ROW_LENGTH. Thus, we have to iterate over all rows and either:
  //   //       - Copy the selected parts of the rows to contiguous memory first, then call glTexSubImage2D() once, or
  //   //       - Call glTexSubImage2D() once for each row (as currently implemented).
  //   //       We could also make a special case for updating the whole width of the texture, where we would always be able to do a single glTexSubImage2D() call once without copying row data around.
  //   //       TODO: Profile which of these is faster.
  //   for (int row = rect[1]; row < rect[3]; ++ row) {
  //     gl.glTexSubImage2D(GL_TEXTURE_2D, /*level*/ 0, /*xoffset*/ rect[0], /*yoffset*/ row, updateWidth, /*updateHeight*/ 1, /*GL_RED*/ 0x1903, GL_UNSIGNED_BYTE, data + rect[0] + row * width);
  //   }
  // }
  
  CHECK_OPENGL_NO_ERROR();
}

void FontStashOpenGL::RenderDelete() {
  CHECK_OPENGL_NO_ERROR();
  
  textures.clear();
  geometryBuffer.Destroy();
  
  CHECK_OPENGL_NO_ERROR();
}

void FontStashOpenGL::ErrorCallback(int error, int val) {
  if (error == FONS_ATLAS_FULL) {
    fonsResetAtlas(context, width, height);
  } else {
    LOG(ERROR) << "Unhandled fontstash error: " << error << ", val: " << val;
  }
}

int FontStashOpenGL::RenderCreate(void* userPtr, int width, int height, int textureIndex) {
  FontStashOpenGL* fontStash = reinterpret_cast<FontStashOpenGL*>(userPtr);
  return fontStash->RenderCreate(width, height, textureIndex);
}

int FontStashOpenGL::RenderResize(void* userPtr, int width, int height, int textureIndex) {
  FontStashOpenGL* fontStash = reinterpret_cast<FontStashOpenGL*>(userPtr);
  return fontStash->RenderResize(width, height, textureIndex);
}

void FontStashOpenGL::RenderUpdate(void* userPtr, int* rect, const unsigned char* data) {
  FontStashOpenGL* fontStash = reinterpret_cast<FontStashOpenGL*>(userPtr);
  fontStash->RenderUpdate(rect, data);
}

void FontStashOpenGL::RenderDelete(void* userPtr) {
  FontStashOpenGL* fontStash = reinterpret_cast<FontStashOpenGL*>(userPtr);
  fontStash->RenderDelete();
}

void FontStashOpenGL::ErrorCallback(void* userPtr, int error, int val) {
  FontStashOpenGL* fontStash = reinterpret_cast<FontStashOpenGL*>(userPtr);
  fontStash->ErrorCallback(error, val);
}

}

#endif
