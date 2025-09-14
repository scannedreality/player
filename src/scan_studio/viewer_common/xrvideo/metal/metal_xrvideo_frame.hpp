#pragma once
#ifdef __APPLE__

#include <condition_variable>
#include <mutex>
#include <vector>

#include <Metal/Metal.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_shader.h"
#include "scan_studio/viewer_common/xrvideo/xrvideo_frame.hpp"

namespace scan_studio {
using namespace vis;

class MetalXRVideo;
class TextureFramePromise;

/// Represents a single frame of an XRVideo, for rendering with Metal.
class MetalXRVideoFrame : public XRVideoFrame {
 public:
  inline MetalXRVideoFrame() {}
  
  inline MetalXRVideoFrame(MetalXRVideoFrame&& other) = default;
  inline MetalXRVideoFrame& operator= (MetalXRVideoFrame&& other) = default;
  
  inline MetalXRVideoFrame(const MetalXRVideoFrame& other) = delete;
  inline MetalXRVideoFrame& operator= (const MetalXRVideoFrame& other) = delete;
  
  void Configure(
      MTL::CommandQueue* transferQueue,
      MTL::Device* device);
  
  void UseExternalBuffers(MTL::Buffer* vertexBuffer, MTL::Buffer* indexBuffer, MTL::Buffer* storageBuffer, MTL::Buffer* alphaBuffer);
  // void UseExternalTextures(MTL::Texture* textureLuma, MTL::Texture* textureChromaU, MTL::Texture* textureChromaV);
  
  bool InitializeTextures(u32 textureWidth, u32 textureHeight);
  
  bool Initialize(
      const XRVideoFrameMetadata& metadata,
      const u8* contentPtr,
      TextureFramePromise* textureFramePromise,
      XRVideoDecodingContext* decodingContext,
      bool verboseDecoding);
  void Destroy();
  
  void WaitForResourceTransfers();
  
  /// Adds the rendering commands to the render command encoder.
  /// If this frame is a keyframe, then lastKeyframe may be nullptr.
  void Render(
      MTL::RenderCommandEncoder* encoder,
      bool flipBackFaceCulling,
      bool useSurfaceNormalShading,
      XRVideo_Vertex_InstanceData* instanceData,
      u32 interpolatedDeformationStateBufferIndex,
      const MetalXRVideoFrame* lastKeyframe,
      const MetalXRVideo* xrVideo) const;
  
  inline MTL::Buffer* GetDeformationStateBuffer() { return storageBuffer.get(); }
  
  inline MTL::Texture* TextureLuma() { return textureLuma.get(); }
  inline MTL::Texture* TextureChromaU() { return textureChromaU.get(); }
  inline MTL::Texture* TextureChromaV() { return textureChromaV.get(); }
  
 private:
  /// Vertex buffer on the GPU (only used by keyframes)
  NS::SharedPtr<MTL::Buffer> vertexBuffer;
  
  /// Index buffer on the GPU (only used by keyframes)
  NS::SharedPtr<MTL::Buffer> indexBuffer;
  
  /// Deformation state buffer on the GPU
  NS::SharedPtr<MTL::Buffer> storageBuffer;
  
  /// Alpha buffer on the GPU
  NS::SharedPtr<MTL::Buffer> alphaBuffer;
  
  NS::SharedPtr<MTL::Texture> textureLuma;
  NS::SharedPtr<MTL::Texture> textureChromaU;
  NS::SharedPtr<MTL::Texture> textureChromaV;
  
  bool useExternalBuffers = false;
  // bool useExternalTextures = false;
  
  NS::SharedPtr<MTL::Buffer> verticesStagingBuffer;
  NS::SharedPtr<MTL::Buffer> indicesStagingBuffer;
  NS::SharedPtr<MTL::Buffer> storageStagingBuffer;
  NS::SharedPtr<MTL::Buffer> alphaStagingBuffer;
  NS::SharedPtr<MTL::Buffer> textureStagingBuffer;
  
  struct BlitCompleteSync {
    mutex blitCompleteMutex;
    bool blitComplete;
    condition_variable blitCompleteCondition;
  };
  unique_ptr<BlitCompleteSync> blitCompleteSync;
  
  MTL::CommandQueue* transferQueue;
  MTL::Device* device;
};

}

#endif
