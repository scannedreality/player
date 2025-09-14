#pragma once
#ifdef HAVE_VULKAN

#include <libvis/vulkan/buffer.h>
#include <libvis/vulkan/delayed_delete_queue.h>
#include <libvis/vulkan/descriptors.h>
#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/texture.h>

#include "scan_studio/viewer_common/gfx/fontstash.hpp"

// Generated shader bytecode
#include "scan_studio/viewer_common/generated/gfx/fontstash_vulkan_shader.binding.hpp"

namespace scan_studio {
using namespace vis;

struct FONScontext;
struct FONSVertex;
class FontStashShader;
struct VulkanRenderState;

class FontStashVulkan : public FontStash {
 public:
  ~FontStashVulkan();
  
  /// See enum FONSflags for `flags`.
  virtual bool Initialize(int textureWidth, int textureHeight, RenderState* renderState) override;
  
  virtual void Destroy() override;
  
  /// Returns the assigned ID of the loaded font, or FONS_INVALID in case of an error.
  virtual int LoadFont(const char* name, unique_ptr<InputStream>&& ttfFileStream) override;
  
  virtual inline FONScontext* GetContext() const override { return context; }
  
  virtual void PrepareFrame(RenderState* renderState) override;
  
  bool BufferVertices(const FONSVertex* vertices, int count, VkBuffer* buffer, VkDeviceSize* offset);
  fontstash_vulkan_shader::DescriptorSet0* FindDescriptorSetForRendering(int textureIndex);
  
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
  shared_ptr<VulkanTexture> texture;
  shared_ptr<VulkanBuffer> textureStaging;
  
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
    VulkanBuffer buffer;
    
    /// Used ranges within the buffer, ordered by increasing firstIndex
    vector<UsedRange> usedRanges;
  };
  vector<GeometryBuffer> geometryBuffers;
  
  // Descriptors:
  // We only need one descriptor per texture. Because of this, we simply use a fixed,
  // sufficiently large number of descriptors (much larger than the number of textures that
  // we expect to use concurrently).
  struct DescriptorSet {
    fontstash_vulkan_shader::DescriptorSet0 descriptorSet;
    /// The texture index given by FONS that this descriptor refers to
    int fonsTextureIndex;
    /// The frame index in which this descriptor has been used last
    int lastUsedInFrameIndex;
  };
  VulkanDescriptorPool descriptorPool;
  vector<DescriptorSet> textureDescriptors;
  
  FONScontext* context = nullptr;  // owned object
  
  /// Cached render state for the current frame, set in PrepareFrame().
  VulkanRenderState* thisFramesRenderState = nullptr;
  
  VulkanDelayedDeleteQueue delayedDeleteQueue;
  int frameIndex = 0;
  
  VulkanCommandPool transferPool;
  
  VulkanDevice* device = nullptr;  // not owned
};

}

#endif
