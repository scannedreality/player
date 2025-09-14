#pragma once
#ifdef HAVE_D3D11

#include <condition_variable>
#include <mutex>
#include <vector>

#include <d3d11_4.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_frame.hpp"

namespace scan_studio {
using namespace vis;

class D3D11XRVideo;
class TextureFramePromise;

/// Represents a single frame of an XRVideo, for rendering with D3D11.
class D3D11XRVideoFrame : public XRVideoFrame {
 public:
  inline D3D11XRVideoFrame() {}
  
  inline D3D11XRVideoFrame(D3D11XRVideoFrame&& other) = default;
  inline D3D11XRVideoFrame& operator= (D3D11XRVideoFrame&& other) = default;
  
  inline D3D11XRVideoFrame(const D3D11XRVideoFrame& other) = delete;
  inline D3D11XRVideoFrame& operator= (const D3D11XRVideoFrame& other) = delete;
  
  void Configure(ID3D11DeviceContext4* deviceContext, ID3D11Device5* device);
  
  bool UseExternalBuffers(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, ID3D11Buffer* storageBuffer, ID3D11Buffer* alphaBuffer);
  
  bool InitializeTextures(u32 textureWidth, u32 textureHeight);
  
  bool Initialize(
      const XRVideoFrameMetadata& metadata,
      const u8* contentPtr,
      TextureFramePromise* textureFramePromise,
      XRVideoDecodingContext* decodingContext,
      bool verboseDecoding);
  void Destroy();
  
  void WaitForResourceTransfers();
  
  /// TODO: Rendering is not implemented for the D3D11 path yet since so far, we only use that for the Unity plugin, where Unity does the rendering
  /// If this frame is a keyframe, then lastKeyframe may be nullptr.
  // void Render(
  //     MTL::RenderCommandEncoder* encoder,
  //     bool flipBackFaceCulling,
  //     bool useSurfaceNormalShading,
  //     XRVideo_Vertex_InstanceData* instanceData,
  //     u32 interpolatedDeformationStateBufferIndex,
  //     const D3D11XRVideoFrame* lastKeyframe,
  //     const D3D11XRVideo* xrVideo) const;
  
  inline ID3D11ShaderResourceView* StorageBufferView() { return storageBufferView.get(); }
  
  ID3D11Texture2D* TextureLuma() { return textureLuma.get(); }
  ID3D11Texture2D* TextureChromaU() { return textureChromaU.get(); }
  ID3D11Texture2D* TextureChromaV() { return textureChromaV.get(); }
  
 private:
  /// Vertex buffer on the GPU (only used by keyframes)
  shared_ptr<ID3D11Buffer> vertexBuffer;
  
  /// Index buffer on the GPU (only used by keyframes)
  shared_ptr<ID3D11Buffer> indexBuffer;
  
  /// Deformation state buffer on the GPU
  shared_ptr<ID3D11Buffer> storageBuffer;
  shared_ptr<ID3D11ShaderResourceView> storageBufferView;

  /// Alpha buffer on the GPU
  shared_ptr<ID3D11Buffer> alphaBuffer;
  
  shared_ptr<ID3D11Texture2D> textureLuma;
  shared_ptr<ID3D11Texture2D> textureChromaU;
  shared_ptr<ID3D11Texture2D> textureChromaV;
  
  bool useStagingBuffers;
  bool useExternalBuffers = false;
  D3D11_BUFFER_DESC externalVertexBufferDesc;
  D3D11_BUFFER_DESC externalIndexBufferDesc;
  D3D11_BUFFER_DESC externalStorageBufferDesc;
  D3D11_BUFFER_DESC externalAlphaBufferDesc;
  
  shared_ptr<ID3D11Buffer> verticesStagingBuffer;
  shared_ptr<ID3D11Buffer> indicesStagingBuffer;
  shared_ptr<ID3D11Buffer> storageStagingBuffer;
  shared_ptr<ID3D11Buffer> alphaStagingBuffer;
  shared_ptr<ID3D11Texture2D> stagingTextureLuma;
  shared_ptr<ID3D11Texture2D> stagingTextureChromaU;
  shared_ptr<ID3D11Texture2D> stagingTextureChromaV;
  
  vector<u8> vertexAlpha;

  shared_ptr<ID3D11Fence> transferCompleteFence;
  UINT64 nextTransferCompleteFenceValue = 0;
  HANDLE transferCompleteEvent = nullptr;
  
  shared_ptr<ID3D11DeviceContext4> deviceContext;
  shared_ptr<ID3D11Device5> device;
};

}

#endif
