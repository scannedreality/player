#ifdef __APPLE__

#include "scan_studio/viewer_common/gfx/fontstash_metal.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include <loguru.hpp>

#include <libvis/io/input_stream.h>

#include "scan_studio/viewer_common/gfx/fontstash_library.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

FontStashMetal::~FontStashMetal() {
  Destroy();
}

bool FontStashMetal::Initialize(int textureWidth, int textureHeight, RenderState* renderState) {
  MetalRenderState* metalState = renderState->AsMetalRenderState();
  
  device = metalState->device;
  thisFramesRenderState = metalState;
  
  // Initialize FontStash (this already calls the create callback, so the Metal objects must be initialized at this point)
	FONSparams params;
  
	memset(&params, 0, sizeof(params));
	params.width = textureWidth;
	params.height = textureHeight;
	params.flags = FONS_ZERO_TOPLEFT;
	params.renderCreate = &FontStashMetal::RenderCreate;
	params.renderResize = &FontStashMetal::RenderResize;
	params.renderUpdate = &FontStashMetal::RenderUpdate;
	params.renderDelete = &FontStashMetal::RenderDelete;
	params.userPtr = this;
  
	context = fonsCreateInternal(&params);
	if (context == nullptr) { return false; }
	
	fonsSetErrorCallback(context, &FontStashMetal::ErrorCallback, this);
	
	return true;
}

void FontStashMetal::Destroy() {
  if (context) {
    fonsDeleteInternal(context);
    context = nullptr;
  }
}

int FontStashMetal::LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) {
  // TODO: We should share the file loading code with FontStashOpenGL, it is identical
  if (context == nullptr) { LOG(ERROR) << "Initialize() has not been called yet"; return false; }
  
  vector<u8> data;
  if (ttfFileStream == nullptr) { LOG(ERROR) << "Tried to load a null file"; return false; }
  if (!ttfFileStream->ReadAll(&data)) { return false; }
  
  return fonsAddFontMem(context, name, std::move(data));
}

void FontStashMetal::PrepareFrame(RenderState* renderState) {
  thisFramesRenderState = renderState->AsMetalRenderState();
  
  ++ frameIndex;
}

bool FontStashMetal::BufferVertices(const FONSVertex* vertices, int count, MTL::Buffer** buffer, u64* offset) {
  int geometryBufferIndex;
  int firstIndex;
  if (!FindOrMakeSpaceForVertices(count, &geometryBufferIndex, &firstIndex)) {
    LOG(ERROR) << "Failed to find or make space for " << count << " vertices";
    return false;
  }
  
  GeometryBuffer& geom = geometryBuffers[geometryBufferIndex];
  memcpy(static_cast<FONSVertex*>(geom.buffer->contents()) + firstIndex, vertices, count * sizeof(FONSVertex));
  
  *buffer = geom.buffer.get();
  *offset = firstIndex * sizeof(FONSVertex);
  return true;
}

MTL::Texture* FontStashMetal::FindTextureForRendering(int textureIndex) {
  if (textureIndex == currentFonsTextureIndex) {
    return texture.get();
  }
  
  for (auto& descriptor : oldTextureDescriptors) {
    if (descriptor.fonsTextureIndex == textureIndex) {
      return descriptor.texture.get();
    }
  }
  
  LOG(ERROR) << "Could not find texture for textureIndex " << textureIndex;
  return nullptr;
}

bool FontStashMetal::FindOrMakeSpaceForVertices(int count, int* geometryBufferIndex, int* firstIndex) {
  for (*geometryBufferIndex = 0; *geometryBufferIndex < geometryBuffers.size(); ++ *geometryBufferIndex) {
    GeometryBuffer& geom = geometryBuffers[*geometryBufferIndex];
    
    // Clear outdated used ranges for this geometry buffer
    for (auto it = geom.usedRanges.begin(); it != geom.usedRanges.end(); ) {
      if (it->usedInFrameIndex + thisFramesRenderState->framesInFlightCount <= frameIndex) {
        it = geom.usedRanges.erase(it);
      } else {
        ++ it;
      }
    }
    
    // Search for a free range within this geometry buffer
    *firstIndex = 0;
    
    for (auto it = geom.usedRanges.begin(); it != geom.usedRanges.end(); ++ it) {
      // Is the range [*firstIndex; it->firstIndex - 1] large enough?
      if (it->firstIndex - *firstIndex >= count) {
        // Place the vertices at the start of the free range
        geom.usedRanges.emplace(it, *firstIndex, *firstIndex + count - 1, frameIndex);
        return true;
      }
      
      *firstIndex = it->lastIndex + 1;
    }
    
    // Is the range [*firstIndex; bufferSize - 1] large enough?
    if (geom.buffer->length() / sizeof(FONSVertex) - *firstIndex >= count) {
      // Place the vertices at the start of the free range
      geom.usedRanges.emplace_back(*firstIndex, *firstIndex + count - 1, frameIndex);
      return true;
    }
  }
  
  // There is no free space in the available geometryBuffers. Allocate a new buffer.
  // Since we always copy new data to these buffers and render each range only once,
  // we use host-visible and coherent memory (without separate staging buffers).
  geometryBuffers.emplace_back();
  GeometryBuffer& geom = geometryBuffers.back();
  
  constexpr int kMinBufferVertexCount = 10 * 1024;
  const int bufferVertexCount = max(kMinBufferVertexCount, count);
  
  geom.buffer = NS::TransferPtr(device->newBuffer(bufferVertexCount * sizeof(FONSVertex), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined));
  if (!geom.buffer) { LOG(ERROR) << "Failed to allocate GPU buffer"; geometryBuffers.pop_back(); return false; }
  
  *geometryBufferIndex = geometryBuffers.size() - 1;
  *firstIndex = 0;
  geom.usedRanges.emplace_back(*firstIndex, *firstIndex + count - 1, frameIndex);
  return true;
}

int FontStashMetal::RenderCreate(int width, int height, int textureIndex) {
  if (texture) {
    // TODO: I think we can simply delete the old texture (if any) here without a delayed delete queue as in Vulkan, and without causing the CPU to wait for the GPU to finish using it ... right?
    oldTextureDescriptors.push_back({std::move(texture), currentFonsTextureIndex});
  }
  
  auto textureDesc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR8Unorm, width, height, /*mipmapped*/ false);
  textureDesc->setStorageMode(device->hasUnifiedMemory() ? MTL::StorageModeShared : MTL::StorageModeManaged);
  textureDesc->setUsage(MTL::TextureUsageShaderRead);
  textureDesc->setCpuCacheMode(MTL::CPUCacheMode::CPUCacheModeWriteCombined);
  texture = NS::TransferPtr(device->newTexture(textureDesc));
  
  currentFonsTextureIndex = textureIndex;
  this->width = width;
  this->height = height;
  
  return 1;
}

int FontStashMetal::RenderResize(int width, int height, int textureIndex) {
  return RenderCreate(width, height, textureIndex);
}

void FontStashMetal::RenderUpdate(int* rect, const unsigned char* data) {
  if (!texture) { return; }
  
  const int updateWidth = rect[2] - rect[0];
  const int updateHeight = rect[3] - rect[1];
  
  texture->replaceRegion(
      MTL::Region(rect[0], rect[1], updateWidth, updateHeight),
      /*level*/ 0,
      data + rect[0] + rect[1] * width,
      /*bytesPerRow*/ width);
}

void FontStashMetal::RenderDelete() {
  texture.reset();
  oldTextureDescriptors.clear();
  geometryBuffers.clear();
}

void FontStashMetal::ErrorCallback(int error, int val) {
  if (error == FONS_ATLAS_FULL) {
    fonsResetAtlas(context, width, height);  // calls RenderResize(), thus will allocate a new texture
  } else {
    LOG(ERROR) << "Unhandled fontstash error: " << error << ", val: " << val;
  }
}

int FontStashMetal::RenderCreate(void* userPtr, int width, int height, int textureIndex) {
  FontStashMetal* fontStash = reinterpret_cast<FontStashMetal*>(userPtr);
  return fontStash->RenderCreate(width, height, textureIndex);
}

int FontStashMetal::RenderResize(void* userPtr, int width, int height, int textureIndex) {
  FontStashMetal* fontStash = reinterpret_cast<FontStashMetal*>(userPtr);
  return fontStash->RenderResize(width, height, textureIndex);
}

void FontStashMetal::RenderUpdate(void* userPtr, int* rect, const unsigned char* data) {
  FontStashMetal* fontStash = reinterpret_cast<FontStashMetal*>(userPtr);
  fontStash->RenderUpdate(rect, data);
}

void FontStashMetal::RenderDelete(void* userPtr) {
  FontStashMetal* fontStash = reinterpret_cast<FontStashMetal*>(userPtr);
  fontStash->RenderDelete();
}

void FontStashMetal::ErrorCallback(void* userPtr, int error, int val) {
  FontStashMetal* fontStash = reinterpret_cast<FontStashMetal*>(userPtr);
  fontStash->ErrorCallback(error, val);
}

}

#endif
