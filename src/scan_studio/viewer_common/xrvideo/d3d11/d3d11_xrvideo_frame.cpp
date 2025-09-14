#ifdef HAVE_D3D11
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo_frame.hpp"

#include <d3d11_3.h>

#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>

#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo_common_resources.hpp"

namespace scan_studio {

void D3D11XRVideoFrame::Configure(ID3D11DeviceContext4* deviceContext, ID3D11Device5* device) {
  deviceContext->AddRef();
  this->deviceContext.reset(deviceContext, [](ID3D11DeviceContext4* context) { context->Release(); });
  
  device->AddRef();
  this->device.reset(device, [](ID3D11Device5* device) { device->Release(); });
}

bool D3D11XRVideoFrame::UseExternalBuffers(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, ID3D11Buffer* storageBuffer, ID3D11Buffer* alphaBuffer) {
  /*const ULONG newVertexBufferRefCount =*/ vertexBuffer->AddRef();
  this->vertexBuffer.reset(vertexBuffer, [](ID3D11Buffer* buffer) { buffer->Release(); });
  /*const ULONG newIndexBufferRefCount =*/ indexBuffer->AddRef();
  this->indexBuffer.reset(indexBuffer, [](ID3D11Buffer* buffer) { buffer->Release(); });
  /*const ULONG newStorageBufferRefCount =*/ storageBuffer->AddRef();
  this->storageBuffer.reset(storageBuffer, [](ID3D11Buffer* buffer) { buffer->Release(); });
  /*const ULONG newAlphaBufferRefCount =*/ alphaBuffer->AddRef();
  this->alphaBuffer.reset(alphaBuffer, [](ID3D11Buffer* buffer) { buffer->Release(); });

  // // Debug: Print some information about the externally received resources
  // LOG(1) << "newVertexBufferRefCount: " << newVertexBufferRefCount;
  // LOG(1) << "newIndexBufferRefCount: " << newIndexBufferRefCount;
  // LOG(1) << "newStorageBufferRefCount: " << newStorageBufferRefCount;
  // LOG(1) << "newAlphaBufferRefCount: " << newAlphaBufferRefCount;
  // 
  // auto printResourceInfo = [](const char* name, ID3D11Buffer* buffer) {
  //   D3D11_BUFFER_DESC desc;
  //   buffer->GetDesc(&desc);
  //   LOG(1) << name << ": Usage: " << desc.Usage << ", BindFlags: " << desc.BindFlags
  //          << ", CPUAccessFlags: " << desc.CPUAccessFlags << ", MiscFlags: " << desc.MiscFlags << ", StructureByteStride: " << desc.StructureByteStride;
  // };
  // printResourceInfo("Vertex buffer", vertexBuffer);
  // printResourceInfo("Index buffer", indexBuffer);
  // printResourceInfo("Storage buffer", storageBuffer);
  // printResourceInfo("Alpha buffer", alphaBuffer);
  
  useExternalBuffers = true;
  
  // If we got D3D11_USAGE_DYNAMIC buffers, write to them directly, otherwise use staging buffers.
  constexpr int kExternalBufferCount = 4;
  u32 dynamicBufferCount = 0;
  
  vertexBuffer->GetDesc(&externalVertexBufferDesc);
  dynamicBufferCount += (externalVertexBufferDesc.Usage == D3D11_USAGE_DYNAMIC) ? 1 : 0;
  
  indexBuffer->GetDesc(&externalIndexBufferDesc);
  dynamicBufferCount += (externalIndexBufferDesc.Usage == D3D11_USAGE_DYNAMIC) ? 1 : 0;
  
  storageBuffer->GetDesc(&externalStorageBufferDesc);
  dynamicBufferCount += (externalStorageBufferDesc.Usage == D3D11_USAGE_DYNAMIC) ? 1 : 0;
  
  alphaBuffer->GetDesc(&externalAlphaBufferDesc);
  dynamicBufferCount += (externalAlphaBufferDesc.Usage == D3D11_USAGE_DYNAMIC) ? 1 : 0;

  useStagingBuffers = dynamicBufferCount < kExternalBufferCount;
  
  if (!useStagingBuffers) {
    // This is because according to what I have seen so far, Direct3D11 does not seem to support mapping of sub-ranges of buffers:
    LOG(WARNING) << "Using external buffers without staging buffers - this always updates the whole buffers, even if the actual required ranges are smaller";
  }
  if (dynamicBufferCount != 0 && dynamicBufferCount != kExternalBufferCount) {
    LOG(WARNING) << "The provided external buffers are not either all allocated with D3D11_USAGE_DYNAMIC or all without it; the code is not optimized for this case.";
  }
  
  if ((externalStorageBufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0) {
    LOG(ERROR) << "The external storage buffer must be a structured buffer; its actual MiscFlags are: " << externalStorageBufferDesc.MiscFlags;
    return false;
  }
  if (externalStorageBufferDesc.StructureByteStride != sizeof(float)) {
    LOG(ERROR) << "The StructureByteStride of the external storage buffer must be " << sizeof(float) << " but is: " << externalStorageBufferDesc.StructureByteStride;
    return false;
  }
  
  // LOG(1) << "dynamicBufferCount: " << dynamicBufferCount;
  return true;
}

bool D3D11XRVideoFrame::InitializeTextures(u32 textureWidth, u32 textureHeight) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = textureWidth;
  desc.Height = textureHeight;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;
  
  ID3D11Texture2D* newTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, /*pInitialData*/ nullptr, &newTexture))) {
    LOG(ERROR) << "Failed to allocate textureLuma"; Destroy(); return false;
  }
  textureLuma.reset(newTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  desc.Width = textureWidth / 2;
  desc.Height = textureHeight / 2;
  
  newTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, /*pInitialData*/ nullptr, &newTexture))) {
    LOG(ERROR) << "Failed to allocate textureChromaU"; Destroy(); return false;
  }
  textureChromaU.reset(newTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  newTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, /*pInitialData*/ nullptr, &newTexture))) {
    LOG(ERROR) << "Failed to allocate textureChromaV"; Destroy(); return false;
  }
  textureChromaV.reset(newTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  return true;
}

bool D3D11XRVideoFrame::Initialize(
    const XRVideoFrameMetadata& metadata,
    const u8* contentPtr,
    TextureFramePromise* textureFramePromise,
    XRVideoDecodingContext* decodingContext,
    bool verboseDecoding) {
  if (!device) { return false; }
  
  TimePoint frameLoadingStartTime;
  if (verboseDecoding) {
    frameLoadingStartTime = Clock::now();
  }
  
  if (!transferCompleteFence) {
    ID3D11Fence* newFence = nullptr;
    if (FAILED(device->CreateFence(/*InitialValue*/ nextTransferCompleteFenceValue, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), reinterpret_cast<void**>(&newFence)))) {
      LOG(ERROR) << "Allocating transferCompleteFence failed"; Destroy(); return false;
    }
    transferCompleteFence.reset(newFence, [](ID3D11Fence* fence) { fence->Release(); });
  }
  
  if (!transferCompleteEvent) {
    transferCompleteEvent = CreateEvent(/*lpEventAttributes*/ nullptr, /*bManualReset*/ TRUE, /*bInitialState*/ FALSE, /*lpName*/ nullptr);
    if (transferCompleteEvent == nullptr) {
      LOG(ERROR) << "Allocating transferCompleteEvent failed"; Destroy(); return false;
    }
  }
  
  // In case this frame gets reused and owns its resources, destroy the buffers
  if (!useExternalBuffers) {
    vertexBuffer.reset();
    indexBuffer.reset();
    storageBuffer.reset();
    storageBufferView.reset();
    alphaBuffer.reset();
    
    useStagingBuffers = true;
  }
  
  // Store the metadata
  this->metadata = metadata;
  
  // Initialize staging buffers (if enabled).
  
  // TODO: Staging buffers should be initialized with maximum sizes, managed externally, and given to this class when needed to prevent allocations at runtime
  if (useStagingBuffers) {
    ID3D11Buffer* newBuffer = nullptr;
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    if (metadata.isKeyframe) {
      desc.ByteWidth = metadata.GetRenderableVertexDataSize();
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to allocate verticesStagingBuffer"; Destroy(); return false;
      }
      verticesStagingBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
      
      desc.ByteWidth = metadata.GetIndexDataSize();
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to allocate indicesStagingBuffer"; Destroy(); return false;
      }
      indicesStagingBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
    }
    
    desc.ByteWidth = metadata.GetDeformationStateDataSize();
    if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
      LOG(ERROR) << "Failed to allocate storageStagingBuffer"; Destroy(); return false;
    }
    storageStagingBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
  }
  
  // In contrast to the other resources, we always use staging resources for the textures.
  D3D11_TEXTURE2D_DESC stagingTextureDesc{};
  stagingTextureDesc.Width = metadata.textureWidth;
  stagingTextureDesc.Height = metadata.textureHeight;
  stagingTextureDesc.MipLevels = 1;
  stagingTextureDesc.ArraySize = 1;
  stagingTextureDesc.Format = DXGI_FORMAT_R8_UNORM;
  stagingTextureDesc.SampleDesc.Count = 1;
  stagingTextureDesc.SampleDesc.Quality = 0;
  stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
  stagingTextureDesc.BindFlags = 0;
  stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  stagingTextureDesc.MiscFlags = 0;
  
  ID3D11Texture2D* newStagingTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&stagingTextureDesc, /*pInitialData*/ nullptr, &newStagingTexture))) {
    LOG(ERROR) << "Failed to allocate stagingTextureLuma"; Destroy(); return false;
  }
  stagingTextureLuma.reset(newStagingTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  stagingTextureDesc.Width = metadata.textureWidth / 2;
  stagingTextureDesc.Height = metadata.textureHeight / 2;
  
  newStagingTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&stagingTextureDesc, /*pInitialData*/ nullptr, &newStagingTexture))) {
    LOG(ERROR) << "Failed to allocate stagingTextureChromaU"; Destroy(); return false;
  }
  stagingTextureChromaU.reset(newStagingTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  newStagingTexture = nullptr;
  if (FAILED(device->CreateTexture2D(&stagingTextureDesc, /*pInitialData*/ nullptr, &newStagingTexture))) {
    LOG(ERROR) << "Failed to allocate stagingTextureChromaV"; Destroy(); return false;
  }
  stagingTextureChromaV.reset(newStagingTexture, [](ID3D11Texture2D* texture) { texture->Release(); });
  
  // Initialize final resources (unless in external-resource mode, where we only check that the existing external buffers are sufficiently large)
  if (useExternalBuffers) {
    if (metadata.isKeyframe) {
      if (metadata.GetRenderableVertexDataSize() > externalVertexBufferDesc.ByteWidth) {
        LOG(ERROR) << "External vertex buffer is too small. Available bytes: " << externalVertexBufferDesc.ByteWidth << ". Required bytes: " << metadata.GetRenderableVertexDataSize(); Destroy(); return false;
      }
      if (metadata.GetIndexDataSize() > externalIndexBufferDesc.ByteWidth) {
        LOG(ERROR) << "External index buffer is too small. Available bytes: " << externalIndexBufferDesc.ByteWidth << ". Required bytes: " << metadata.GetIndexDataSize(); Destroy(); return false;
      }
    }
    if (metadata.GetDeformationStateDataSize() > externalStorageBufferDesc.ByteWidth) {
      LOG(ERROR) << "External storage buffer is too small. Available bytes: " << externalStorageBufferDesc.ByteWidth << ". Required bytes: " << metadata.GetDeformationStateDataSize(); Destroy(); return false;
    }
  } else {
    ID3D11Buffer* newBuffer = nullptr;
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DEFAULT;
    
    if (metadata.isKeyframe) {
      desc.ByteWidth = metadata.GetRenderableVertexDataSize();
      desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to allocate vertexBuffer"; Destroy(); return false;
      }
      vertexBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
      
      desc.ByteWidth = metadata.GetIndexDataSize();
      desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to allocate indexBuffer"; Destroy(); return false;
      }
      indexBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
    }
    
    desc.ByteWidth = metadata.GetDeformationStateDataSize();
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(float);
    if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
      LOG(ERROR) << "Failed to allocate storageBuffer"; Destroy(); return false;
    }
    storageBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
  }
  
  if (!storageBufferView) {
    D3D11_BUFFER_DESC bufDesc = {};
    storageBuffer->GetDesc(&bufDesc);
    
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.FirstElement = 0;
    desc.BufferEx.NumElements = bufDesc.ByteWidth / bufDesc.StructureByteStride;
    
    ID3D11ShaderResourceView* newView;
    if (FAILED(device->CreateShaderResourceView(storageBuffer.get(), &desc, &newView))) {
      LOG(ERROR) << "Failed to allocate storageBufferView"; Destroy(); return false;
    }
    storageBufferView.reset(newView, [](ID3D11ShaderResourceView* view) { view->Release(); });
  }
  
  // Initialize textures (if not already allocated, e.g., when reusing this frame)
  if (!textureLuma || !textureChromaU || !textureChromaV) {
    if (!InitializeTextures(metadata.textureWidth, metadata.textureHeight)) {
      Destroy(); return false;
    }
  }
  
  // Map all relevant resources
  D3D11_MAPPED_SUBRESOURCE mappedVertices;
  D3D11_MAPPED_SUBRESOURCE mappedIndices;
  D3D11_MAPPED_SUBRESOURCE mappedStorage;
  D3D11_MAPPED_SUBRESOURCE mappedTextureLuma;
  D3D11_MAPPED_SUBRESOURCE mappedTextureChromaU;
  D3D11_MAPPED_SUBRESOURCE mappedTextureChromaV;
  
  if (metadata.isKeyframe) {
    memset(&mappedVertices, 0, sizeof(mappedVertices));
    if (FAILED(deviceContext->Map(
        (useStagingBuffers ? verticesStagingBuffer : vertexBuffer).get(), /*Subresource*/ 0, useStagingBuffers ? D3D11_MAP_WRITE : D3D11_MAP_WRITE_DISCARD, /*MapFlags*/ 0, &mappedVertices))) {
      LOG(ERROR) << "Failed to map vertex (staging) buffer";
    }
    
    memset(&mappedIndices, 0, sizeof(mappedIndices));
    if (FAILED(deviceContext->Map(
        (useStagingBuffers ? indicesStagingBuffer : indexBuffer).get(), /*Subresource*/ 0, useStagingBuffers ? D3D11_MAP_WRITE : D3D11_MAP_WRITE_DISCARD, /*MapFlags*/ 0, &mappedIndices))) {
      LOG(ERROR) << "Failed to map index (staging) buffer";
    }
  }
  
  memset(&mappedStorage, 0, sizeof(mappedStorage));
  if (FAILED(deviceContext->Map(
      (useStagingBuffers ? storageStagingBuffer : storageBuffer).get(), /*Subresource*/ 0, useStagingBuffers ? D3D11_MAP_WRITE : D3D11_MAP_WRITE_DISCARD, /*MapFlags*/ 0, &mappedStorage))) {
    LOG(ERROR) << "Failed to map storage (staging) buffer";
  }
  
  memset(&mappedTextureLuma, 0, sizeof(mappedTextureLuma));
  if (FAILED(deviceContext->Map(stagingTextureLuma.get(), /*Subresource*/ 0, D3D11_MAP_WRITE, /*MapFlags*/ 0, &mappedTextureLuma))) {
    LOG(ERROR) << "Failed to map staging texture";
  }
  
  memset(&mappedTextureChromaU, 0, sizeof(mappedTextureChromaU));
  if (FAILED(deviceContext->Map(stagingTextureChromaU.get(), /*Subresource*/ 0, D3D11_MAP_WRITE, /*MapFlags*/ 0, &mappedTextureChromaU))) {
    LOG(ERROR) << "Failed to map staging texture";
  }
  
  memset(&mappedTextureChromaV, 0, sizeof(mappedTextureChromaV));
  if (FAILED(deviceContext->Map(stagingTextureChromaV.get(), /*Subresource*/ 0, D3D11_MAP_WRITE, /*MapFlags*/ 0, &mappedTextureChromaV))) {
    LOG(ERROR) << "Failed to map staging texture";
  }
  
  // Decompress the frame data
  if (!XRVideoDecompressContent(
      contentPtr, metadata, decodingContext,
      metadata.isKeyframe ? mappedVertices.pData : nullptr,
      static_cast<u16*>(metadata.isKeyframe ? mappedIndices.pData : nullptr),
      static_cast<float*>(mappedStorage.pData),
      /*outDuplicatedVertexSourceIndices*/ nullptr,
      &vertexAlpha,
      verboseDecoding)) {
    LOG(ERROR) << "Failed to decompress XRVideo content";
    Destroy(); return false;
  }

  // Allocate, map, and copy to alpha (staging) buffer
  // (since we only know this buffer's size after XRVideoDecompressContent()).
  // Note: When copying the data to the actual buffer later, the width of the source box must be a multiple of the destination resource structure stride (4).
  const u32 vertexAlphaSize = vertexAlpha.size() * sizeof(*vertexAlpha.data());
  const u32 vertexAlphaCopySize = ((vertexAlphaSize + 3) / 4) * 4;

  if (vertexAlphaSize > 0) {
    if (useStagingBuffers) {
      ID3D11Buffer* newBuffer = nullptr;
      D3D11_BUFFER_DESC desc = {};
      desc.Usage = D3D11_USAGE_STAGING;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      desc.ByteWidth = vertexAlphaCopySize;
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to allocate alphaStagingBuffer"; Destroy(); return false;
      }
      alphaStagingBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
    }

    if (useExternalBuffers) {
      if (vertexAlphaCopySize > externalAlphaBufferDesc.ByteWidth) {
        LOG(ERROR) << "External alpha buffer is too small. Available bytes: " << externalAlphaBufferDesc.ByteWidth << ". Required bytes: " << vertexAlphaCopySize; Destroy(); return false;
      }
    } else {
      LOG(FATAL) << "Only the useExternalBuffers path is implemented currently";
      // ID3D11Buffer* newBuffer = nullptr;
      // D3D11_BUFFER_DESC desc = {};
      // desc.Usage = D3D11_USAGE_DEFAULT;
      // desc.ByteWidth = vertexAlphaSize;
      // desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      // desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      // desc.StructureByteStride = sizeof(float);
      // if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
      //   LOG(ERROR) << "Failed to allocate alphaBuffer"; Destroy(); return false;
      // }
      // alphaBuffer.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
    }

    D3D11_MAPPED_SUBRESOURCE mappedAlpha;
    memset(&mappedAlpha, 0, sizeof(mappedAlpha));
    if (FAILED(deviceContext->Map(
        (useStagingBuffers ? alphaStagingBuffer : alphaBuffer).get(), /*Subresource*/ 0, useStagingBuffers ? D3D11_MAP_WRITE : D3D11_MAP_WRITE_DISCARD, /*MapFlags*/ 0, &mappedAlpha))) {
      LOG(ERROR) << "Failed to map alpha (staging) buffer";
    }

    memcpy(mappedAlpha.pData, vertexAlpha.data(), vertexAlphaSize);
  }
  
  // Get the texture data
  if (!textureFramePromise->Wait()) {
    Destroy(); return false;
  }
  auto textureData = textureFramePromise->Take();
  // TODO: Try to use zero-copy to improve performance
  if (textureData) {
    XRVideoCopyTexture(*textureData, static_cast<u8*>(mappedTextureLuma.pData), static_cast<u8*>(mappedTextureChromaU.pData), static_cast<u8*>(mappedTextureChromaV.pData), verboseDecoding);
  } else {
    memset(mappedTextureLuma.pData, 0, metadata.textureWidth * metadata.textureHeight);
    memset(mappedTextureChromaU.pData, 0, (metadata.textureWidth * metadata.textureHeight) / 4);
    memset(mappedTextureChromaV.pData, 0, (metadata.textureWidth * metadata.textureHeight) / 4);
  }
  textureData.reset();
  
  // Upload the data in the staging buffers to the GPU.
  if (metadata.isKeyframe) {
    deviceContext->Unmap((useStagingBuffers ? verticesStagingBuffer : vertexBuffer).get(), /*Subresource*/ 0);
    deviceContext->Unmap((useStagingBuffers ? indicesStagingBuffer : indexBuffer).get(), /*Subresource*/ 0);
  }
  deviceContext->Unmap((useStagingBuffers ? storageStagingBuffer : storageBuffer).get(), /*Subresource*/ 0);
  if (vertexAlphaSize > 0) {
    deviceContext->Unmap((useStagingBuffers ? alphaStagingBuffer : alphaBuffer).get(), /*Subresource*/ 0);
  }
  
  deviceContext->Unmap(stagingTextureLuma.get(), /*Subresource*/ 0);
  deviceContext->Unmap(stagingTextureChromaU.get(), /*Subresource*/ 0);
  deviceContext->Unmap(stagingTextureChromaV.get(), /*Subresource*/ 0);
  
  if (useStagingBuffers) {
    if (metadata.isKeyframe) {
      const D3D11_BOX verticesBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ metadata.GetRenderableVertexDataSize(), /*bottom*/ 1, /*back*/ 1};
      deviceContext->CopySubresourceRegion(
          vertexBuffer.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
          verticesStagingBuffer.get(), /*SrcSubresource*/ 0, &verticesBox);
      
      const D3D11_BOX indicesBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ metadata.GetIndexDataSize(), /*bottom*/ 1, /*back*/ 1};
      deviceContext->CopySubresourceRegion(
          indexBuffer.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
          indicesStagingBuffer.get(), /*SrcSubresource*/ 0, &indicesBox);
    }
    
    const D3D11_BOX storageBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ metadata.GetDeformationStateDataSize(), /*bottom*/ 1, /*back*/ 1};
    deviceContext->CopySubresourceRegion(
        storageBuffer.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
        storageStagingBuffer.get(), /*SrcSubresource*/ 0, &storageBox);

    if (vertexAlphaSize > 0) {
      // Note: The width of the source box must be a multiple of the destination resource structure stride (4)
      const D3D11_BOX alphaBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ vertexAlphaCopySize, /*bottom*/ 1, /*back*/ 1};
      deviceContext->CopySubresourceRegion(
          alphaBuffer.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
          alphaStagingBuffer.get(), /*SrcSubresource*/ 0, &alphaBox);
    }
  }
  
  const D3D11_BOX textureLumaBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ metadata.textureWidth, /*bottom*/ metadata.textureHeight, /*back*/ 1};
  deviceContext->CopySubresourceRegion(
      textureLuma.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
      stagingTextureLuma.get(), /*SrcSubresource*/ 0, &textureLumaBox);
  
  const D3D11_BOX textureChromaBox{/*left*/ 0, /*top*/ 0, /*front*/ 0, /*right*/ metadata.textureWidth / 2, /*bottom*/ metadata.textureHeight / 2, /*back*/ 1};
  
  deviceContext->CopySubresourceRegion(
      textureChromaU.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
      stagingTextureChromaU.get(), /*SrcSubresource*/ 0, &textureChromaBox);
  deviceContext->CopySubresourceRegion(
      textureChromaV.get(), /*DstSubresource*/ 0, /*DstX*/ 0, /*DstY*/ 0, /*DstZ*/ 0,
      stagingTextureChromaV.get(), /*SrcSubresource*/ 0, &textureChromaBox);
  
  // Issue the signal that allows WaitForResourceTransfers() to wait for all transfer commands to finish.
  // NOTE: This will make WaitForResourceTransfers() also wait for all other device commands issued so far,
  //       including external render commands. However, our pipelined decoding allows other XRVideoFrames
  //       to go through Initialize() while that wait takes place. This means that we will get some extra latency
  //       in the decoding compared to other render paths. But decoding likely won't be slowed down overall.
  ++ nextTransferCompleteFenceValue;
  ResetEvent(transferCompleteEvent);
  if (FAILED(transferCompleteFence->SetEventOnCompletion(nextTransferCompleteFenceValue, transferCompleteEvent))) {
    LOG(ERROR) << "SetEventOnCompletion() failed";
    // TODO: In principle, we should try to handle this error, but probably, there is no way to continue anyway if this happens.
  }
  deviceContext->Signal(transferCompleteFence.get(), nextTransferCompleteFenceValue);
  
  if (verboseDecoding) {
    const TimePoint frameLoadingEndTime = Clock::now();
    LOG(1) << "D3D11XRVideoFrame: Frame loading time without I/O, with texture wait: " << (MillisecondsDuration(frameLoadingEndTime - frameLoadingStartTime).count()) << " ms";
  }
  return true;
}

void D3D11XRVideoFrame::Destroy() {
  verticesStagingBuffer.reset();
  indicesStagingBuffer.reset();
  
  storageStagingBuffer.reset();
  alphaStagingBuffer.reset();
  
  vertexBuffer.reset();
  indexBuffer.reset();
  storageBufferView.reset();
  storageBuffer.reset();
  alphaBuffer.reset();
  
  stagingTextureLuma.reset();
  stagingTextureChromaU.reset();
  stagingTextureChromaV.reset();
  
  textureLuma.reset();
  textureChromaU.reset();
  textureChromaV.reset();
  
  transferCompleteFence.reset();
  
  if (transferCompleteEvent) {
    CloseHandle(transferCompleteEvent);
    transferCompleteEvent = nullptr;
  }
  
  deviceContext.reset();
  device.reset();
}

void D3D11XRVideoFrame::WaitForResourceTransfers() {
  WaitForSingleObject(transferCompleteEvent, INFINITE);
  
  verticesStagingBuffer.reset();
  indicesStagingBuffer.reset();
  storageStagingBuffer.reset();
  alphaStagingBuffer.reset();
  
  stagingTextureLuma.reset();
  stagingTextureChromaU.reset();
  stagingTextureChromaV.reset();

  vertexAlpha = vector<u8>();
}

}

#endif
