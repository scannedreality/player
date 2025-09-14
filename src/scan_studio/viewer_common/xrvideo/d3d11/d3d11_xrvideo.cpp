#ifdef HAVE_D3D11
#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo.hpp"

#include "scan_studio/viewer_common/xrvideo/d3d11/d3d11_xrvideo_common_resources.hpp"
#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

D3D11XRVideo::D3D11XRVideo(
    int framesInFlightCount,
    ID3D11DeviceContext4* deviceContext,
    ID3D11Device5* device)
    : framesInFlightCount(framesInFlightCount) {
  deviceContext->AddRef();
  this->deviceContext.reset(deviceContext, [](ID3D11DeviceContext4* context) { context->Release(); });
  
  device->AddRef();
  this->device.reset(device, [](ID3D11Device5* device) { device->Release(); });
}

D3D11XRVideo::~D3D11XRVideo() {
  Destroy();
}

void D3D11XRVideo::UseExternalResourceMode(int interpolatedDeformationStateBufferCount, ID3D11Buffer** interpolatedDeformationStateBuffers) {
  interpolatedDeformationStates.resize(interpolatedDeformationStateBufferCount);
  
  for (int i = 0; i < interpolatedDeformationStateBufferCount; ++ i) {
    interpolatedDeformationStateBuffers[i]->AddRef();
    interpolatedDeformationStates[i].reset(interpolatedDeformationStateBuffers[i], [](ID3D11Buffer* buf) { buf->Release(); });
  }
  
  externalResourceMode = true;
}

void D3D11XRVideo::Destroy() {
  readingThread.RequestThreadToExit();
  videoThread.RequestThreadToExit();
  decodingThread.RequestThreadToExit();
  transferThread.RequestThreadToExit();
  
  framesLockedForRendering = vector<ReadLockedCachedFrame<D3D11XRVideoFrame>>();
  
  readingThread.WaitForThreadToExit();
  videoThread.WaitForThreadToExit();
  decodingThread.WaitForThreadToExit();
  transferThread.WaitForThreadToExit();
  
  decodingThread.Destroy();
  transferThread.Destroy(/*finishAllTransfers*/ true);
  
  decodedFrameCache.Destroy();
  
  interpolatedDeformationStateCB.reset();
  interpolatedDeformationStateViews.clear();
  interpolatedDeformationStates.clear();
  
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  
  deviceContext.reset();
  device.reset();
}

unique_ptr<XRVideoRenderLock> D3D11XRVideo::CreateRenderLock() {
  decodedFrameCache.Lock();
  
  if (framesLockedForRendering.empty()) {
    decodedFrameCache.Unlock();
    return nullptr;
  }
  
  auto result = unique_ptr<XRVideoRenderLock>(
      new D3D11XRVideoRenderLock(
          this,
          currentFrameInFlightIndex,
          currentIntraFrameTime,
          // copy (rather than move) the framesLockedForRendering into the created render lock object
          vector<ReadLockedCachedFrame<D3D11XRVideoFrame>>(framesLockedForRendering)));
  
  currentFrameInFlightIndex = (currentFrameInFlightIndex + 1) % framesInFlightCount;
  
  decodedFrameCache.Unlock();
  return result;
}

bool D3D11XRVideo::InitializeImpl() {
  // Initialize storage buffers (or only check for their correct size and settings in external resource mode).
  // NOTE: We allocate the maximum size for this, however, this may waste GPU memory.
  //       We could start with a smaller allocation and only grow it if needed.
  //       This is not a priority though: the space needed is only 1.1249 MiB.
  // TODO: I am not sure whether it is really helpful to have a separate buffer for each frame-in-flight here.
  //       If the GPU only starts the deformation state interpolation after the previous frame has finished rendering,
  //       then it would be like for the Z-buffer depth image, where only a single resource is required.
  const u32 maxDeformationNodeCount = pow(2, 13) - 1;
  const u32 storageBufferSize = maxDeformationNodeCount * 12 * sizeof(float);
  
  if (externalResourceMode) {
    if (interpolatedDeformationStates.size() != framesInFlightCount) {
      LOG(ERROR) << "External interpolated deformation states buffers: Invalid framesInFlightCount. Actual: " << interpolatedDeformationStates.size() << ". Required: " << framesInFlightCount;
      return false;
    }
    
    for (int i = 0; i < interpolatedDeformationStates.size(); ++ i) {
      D3D11_BUFFER_DESC desc;
      interpolatedDeformationStates[i]->GetDesc(&desc);
      
      if (desc.ByteWidth < storageBufferSize) {
        LOG(ERROR) << "External interpolated deformation states buffer is too small. Available bytes: " << desc.ByteWidth << ". Required bytes: " << storageBufferSize;
        return false;
      }
      
      if ((desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != D3D11_RESOURCE_MISC_BUFFER_STRUCTURED ||
          (desc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE)) != (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE)) {
        LOG(ERROR) << "External interpolated deformation states buffer has incorrect flags: MiscFlags: " << desc.MiscFlags << ". BindFlags: " << desc.BindFlags;
        return false;
      }
    }
  } else {
    interpolatedDeformationStates.resize(framesInFlightCount);
    
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = storageBufferSize;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(float);
    
    for (int i = 0; i < framesInFlightCount; ++ i) {
      ID3D11Buffer* newBuffer = nullptr;
      if (FAILED(device->CreateBuffer(&desc, /*pInitialData*/ nullptr, &newBuffer))) {
        LOG(ERROR) << "Failed to initialize a storage buffer";
        Destroy(); return false;
      }
      interpolatedDeformationStates[i].reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
    }
  }
  
  // Create the UAV buffer views for interpolatedDeformationStates
  interpolatedDeformationStateViews.resize(interpolatedDeformationStates.size());
  for (int i = 0; i < interpolatedDeformationStates.size(); ++ i) {
    D3D11_BUFFER_DESC bufDesc = {};
    interpolatedDeformationStates[i]->GetDesc(&bufDesc);
    
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_UNKNOWN;  // must be DXGI_FORMAT_UNKNOWN for Structured Buffers
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = bufDesc.ByteWidth / bufDesc.StructureByteStride;
    
    ID3D11UnorderedAccessView* newView;
    if (FAILED(device->CreateUnorderedAccessView(interpolatedDeformationStates[i].get(), &desc, &newView))) {
      LOG(ERROR) << "Failed to allocate interpolatedDeformationStateView";
      Destroy(); return false;
    }
    interpolatedDeformationStateViews[i].reset(newView, [](ID3D11UnorderedAccessView* view) { view->Release(); });
  }
  
  // Create a constant buffer for invoking the compute shader
  D3D11_BUFFER_DESC cbDesc{};
  cbDesc.ByteWidth = ((sizeof(InterpolateDeformationStateConstantBuffer) - 1) / 16 + 1) * 16;  // for constant buffers, this must be a multiple of 16 bytes
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  cbDesc.MiscFlags = 0;
  
  ID3D11Buffer* newBuffer = nullptr;
  if (FAILED(device->CreateBuffer(&cbDesc, /*pInitialData*/ nullptr, &newBuffer))) {
    LOG(ERROR) << "Failed to initialize a constant buffer";
    Destroy(); return false;
  }
  interpolatedDeformationStateCB.reset(newBuffer, [](ID3D11Buffer* buf) { buf->Release(); });
  
  return true;
}

bool D3D11XRVideo::ResizeDecodedFrameCache(int cachedDecodedFrameCount) {
  if (releaseAllExternalFrameResourcesCallback) {
    releaseAllExternalFrameResourcesCallback();
  }
  framesLockedForRendering.clear();
  
  decodedFrameCache.Initialize(cachedDecodedFrameCount);
  for (int cacheItemIndex = 0; cacheItemIndex < cachedDecodedFrameCount; ++ cacheItemIndex) {
    WriteLockedCachedFrame<D3D11XRVideoFrame> lockedFrame = decodedFrameCache.LockCacheItemForWriting(cacheItemIndex);
    lockedFrame.GetFrame()->Configure(deviceContext.get(), device.get());
    if (allocateExternalFrameResourcesCallback) {
      if (!allocateExternalFrameResourcesCallback(cacheItemIndex, lockedFrame.GetFrame())) { return false; }
    }
  }
  
  return true;
}


void D3D11XRVideoRenderLock::PrepareFrame(RenderState* /*renderState*/) {
  auto* commonResources = Video()->CommonResources();
  
  if (framesLockedForRendering.empty()) { return; }
  
  D3D11XRVideoFrame* frame = framesLockedForRendering[0].GetFrame();
  D3D11XRVideoFrame* predecessorFrame = (framesLockedForRendering.size() == 1) ? nullptr : framesLockedForRendering.back().GetFrame();
  
  // Ensure that the XRVideo frames used in rendering stay read-locked while the current rendered frame is in flight
  // TODO: This is not implemented yet for this render path, as it is used only for the Unity plugin, and the Unity plugin has its own mechanism for that
  // Video()->decodedFrameCache.Lock();
  // auto readLockCopies = make_shared<vector<ReadLockedCachedFrame<D3D11XRVideoFrame>>>(framesLockedForRendering);
  // Video()->decodedFrameCache.Unlock();
  // d3d11RenderState->cmdBuf->addCompletedHandler([readLockCopies](MTL::CommandBuffer* /*cmdBuf*/) {
  //   readLockCopies->clear();
  // });
  
  // Run a compute shader to interpolate the deformation state for display
  if (frame->GetMetadata().deformationNodeCount > 0) {
    D3D11XRVideo::InterpolateDeformationStateConstantBuffer constantBuffer;
    constantBuffer.coefficientCount = 12 * frame->GetMetadata().deformationNodeCount;
    constantBuffer.factor = currentIntraFrameTime;
    
    // NOTE: In case Unity also uses multithreaded access to the deviceContext,
    //       then we have to use a critical section here, see:
    //       https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11multithread-enter
    //       We should then query all initial bind values (CSSetShaderResources(), etc.) that we are going
    //       to change at the start of the critical section, and restore them before leaving the critical section.
    shared_ptr<ID3D11DeviceContext4>& deviceContext = Video()->deviceContext;
    
    D3D11_MAPPED_SUBRESOURCE mappedCB;
    if (FAILED(deviceContext->Map(Video()->interpolatedDeformationStateCB.get(), /*Subresource*/ 0, D3D11_MAP_WRITE_DISCARD, /*MapFlags*/ 0, &mappedCB))) {
      LOG(ERROR) << "Failed to map constant buffer";
    }
    memcpy(mappedCB.pData, &constantBuffer, sizeof(constantBuffer));
    deviceContext->Unmap(Video()->interpolatedDeformationStateCB.get(), /*Subresource*/ 0);
    ID3D11Buffer* constantBuffers[1] = { Video()->interpolatedDeformationStateCB.get() };
    deviceContext->CSSetConstantBuffers(/*StartSlot*/ 0, /*NumBuffers*/ 1, constantBuffers);
    
    if (frame->GetMetadata().isKeyframe) {
      // Interpolate from identity to the keyframe's deformation state.
      deviceContext->CSSetShader(commonResources->interpolateDeformationStateFromIdentity.get(), /*ppClassInstances*/ nullptr, /*NumClassInstances*/ 0);
      ID3D11ShaderResourceView* shaderResourceViews[1] = { frame->StorageBufferView() };
      deviceContext->CSSetShaderResources(/*StartSlot*/ 0, /*NumViews*/ 1, shaderResourceViews);
      ID3D11UnorderedAccessView* unorderedAccessViews[1] = { Video()->interpolatedDeformationStateViews[currentFrameInFlightIndex].get() };
      deviceContext->CSSetUnorderedAccessViews(/*StartSlot*/ 0, /*NumUAVs*/ 1, unorderedAccessViews, /*pUAVInitialCounts*/ nullptr);
    } else {
      // Interpolate from the previous frame's deformation state to the current frame's deformation state.
      if (frame->GetMetadata().deformationNodeCount != predecessorFrame->GetMetadata().deformationNodeCount) {
        LOG(ERROR) << "Inconsistency in deformationNodeCount between current and previous frame";
      }
      
      deviceContext->CSSetShader(commonResources->interpolateDeformationState.get(), /*ppClassInstances*/ nullptr, /*NumClassInstances*/ 0);
      ID3D11ShaderResourceView* shaderResourceViews[2] = { predecessorFrame->StorageBufferView(), frame->StorageBufferView() };
      deviceContext->CSSetShaderResources(/*StartSlot*/ 0, /*NumViews*/ 2, shaderResourceViews);
      ID3D11UnorderedAccessView* unorderedAccessViews[1] = { Video()->interpolatedDeformationStateViews[currentFrameInFlightIndex].get() };
      deviceContext->CSSetUnorderedAccessViews(/*StartSlot*/ 0, /*NumUAVs*/ 1, unorderedAccessViews, /*pUAVInitialCounts*/ nullptr);
    }
    
    constexpr int threadgroupSize = 256;  // this is defined in the shaders themselves
    deviceContext->Dispatch((constantBuffer.coefficientCount - 1) / threadgroupSize + 1, 1, 1);
    
    deviceContext->CSSetShader(nullptr, nullptr, 0);
    
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    deviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    deviceContext->CSSetShaderResources(0, 2, nullSRV);
    
    ID3D11Buffer* nullCB[1] = { nullptr };
    deviceContext->CSSetConstantBuffers(0, 1, nullCB);
  }
}

void D3D11XRVideoRenderLock::PrepareView(int /*viewIndex*/, bool /*flipBackFaceCulling*/, bool /*useSurfaceNormalShading*/, RenderState* /*renderState*/) {
  LOG(ERROR) << "Rendering is not implemented for the D3D11 path yet";
}

void D3D11XRVideoRenderLock::RenderView(RenderState* /*renderState*/) {
  LOG(ERROR) << "Rendering is not implemented for the D3D11 path yet";
}

void D3D11XRVideoRenderLock::SetModelViewProjection(int /*viewIndex*/, int /*multiViewIndex*/, const float* /*columnMajorModelViewData*/, const float* /*columnMajorModelViewProjectionData*/) {
  LOG(ERROR) << "Rendering is not implemented for the D3D11 path yet";
}

}

#endif
