#pragma once
#ifdef __APPLE__

#include <vector>

#include <Metal/Metal.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/fontstash.hpp"

namespace scan_studio {
using namespace vis;

struct FONScontext;
struct FONSVertex;
struct MetalRenderState;

class FontStashMetal : public FontStash {
 public:
  ~FontStashMetal();
  
  /// See enum FONSflags for `flags`.
  virtual bool Initialize(int textureWidth, int textureHeight, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  /// Returns the assigned ID of the loaded font, or FONS_INVALID in case of an error.
  virtual int LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) override;
  
  virtual inline FONScontext* GetContext() const override { return context; }
  
  virtual void PrepareFrame(RenderState* renderState) override;
  
  bool BufferVertices(const FONSVertex* vertices, int count, MTL::Buffer** buffer, u64* offset);
  MTL::Texture* FindTextureForRendering(int textureIndex);
  
 private:
  bool FindOrMakeSpaceForVertices(int count, int* geometryBufferIndex, int* firstIndex);
  
  // FontStash callbacks
  int RenderCreate(int width, int height, int textureIndex);
  int RenderResize(int width, int height, int textureIndex);
  void RenderUpdate(int* rect, const unsigned char* data);
  void RenderDelete();
  void ErrorCallback(int error, int val);
  
  static int RenderCreate(void* userPtr, int width, int height, int textureIndex);
  static int RenderResize(void* userPtr, int width, int height, int textureIndex);
  static void RenderUpdate(void* userPtr, int* rect, const unsigned char* data);
  static void RenderDelete(void* userPtr);
  static void ErrorCallback(void* userPtr, int error, int val);
  
  int width;
  int height;
  int currentFonsTextureIndex;
  NS::SharedPtr<MTL::Texture> texture;
  
  struct UsedRange {
    inline UsedRange(int firstIndex, int lastIndex, int usedInFrameIndex)
        : firstIndex(firstIndex),
          lastIndex(lastIndex),
          usedInFrameIndex(usedInFrameIndex) {}
    
    int firstIndex;
    int lastIndex;
    
    int usedInFrameIndex;
  };
  
  struct GeometryBuffer {
    NS::SharedPtr<MTL::Buffer> buffer;
    
    /// Used ranges within the buffer, ordered by increasing firstIndex
    vector<UsedRange> usedRanges;
  };
  vector<GeometryBuffer> geometryBuffers;
  
  // References to old textures that are still being used in the current frame
  struct DescriptorSet {
    NS::SharedPtr<MTL::Texture> texture;
    /// The texture index given by FONS that this descriptor refers to
    int fonsTextureIndex;
  };
  vector<DescriptorSet> oldTextureDescriptors;
  
  FONScontext* context = nullptr;  // owned object
  
  /// Cached render state for the current frame, set in PrepareFrame().
  MetalRenderState* thisFramesRenderState = nullptr;
  
  int frameIndex = 0;
  
  MTL::Device* device = nullptr;  // not owned
};

}

#endif
