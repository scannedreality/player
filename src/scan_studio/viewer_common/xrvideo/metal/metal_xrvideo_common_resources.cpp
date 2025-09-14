#ifdef __APPLE__
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_common_resources.hpp"

#include <loguru.hpp>

#include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_shader.h"

#include <QuartzCore/QuartzCore.hpp>

#include "scan_studio/viewer_common/metal/library_cache.hpp"

// Generated shader bytecode
#include "viewer_common_metallib.h"

namespace scan_studio {

MetalXRVideoCommonResources::~MetalXRVideoCommonResources() {
  Destroy();
}

bool MetalXRVideoCommonResources::Initialize(bool outputLinearColors, bool usesInverseZ, MetalRenderPassDescriptor* renderPassDesc, MetalLibraryCache* libraryCache, MTL::Device* device) {
  NS::Error* err;
  
  this->renderPassDesc = *renderPassDesc;
  this->outputLinearColors = outputLinearColors;
  this->usesInverseZ = usesInverseZ;
  
  MTL::Library* library = libraryCache->GetOrCreateLibrary("viewer_common", viewer_common_metallib, viewer_common_metallib_len, device);
  if (!library) { Destroy(); return false; }
  
  // Initialize graphics pipeline for rendering
  for (auto* pipe : {&pipeline/*, &normalShadingPipeline*/}) {
    auto pipelineDesc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
    pipelineDesc->setLabel(NS::String::string((pipe == &pipeline) ? "XRVideoRenderPipeline" : "XRVideoNormalShadingRenderPipeline", NS::UTF8StringEncoding));
    
    auto shaderFunctions = libraryCache->AssignVertexAndFragmentFunctions(
        (pipe == &pipeline) ? "XRVideo_VertexMain" : "XRVideoNormalShading_VertexMain",
        (pipe == &pipeline) ?
            (outputLinearColors ? "XRVideo_FragmentMain_Linear" : "XRVideo_FragmentMain_sRGB") :
            (outputLinearColors ? "XRVideoNormalShading_FragmentMain_Linear" : "XRVideoNormalShading_FragmentMain_sRGB"),
        pipelineDesc.get(),
        "viewer_common", viewer_common_metallib, viewer_common_metallib_len, device);
    if (!shaderFunctions) { return false; }
    
    pipelineDesc->setRasterSampleCount(renderPassDesc->rasterSampleCount);
    
    auto colorAttachmentDesc = pipelineDesc->colorAttachments()->object(0);
    colorAttachmentDesc->setPixelFormat(renderPassDesc->colorFormats[0]);
    
    pipelineDesc->setDepthAttachmentPixelFormat(renderPassDesc->depthFormat);
    pipelineDesc->setStencilAttachmentPixelFormat(renderPassDesc->stencilFormat);
    
    pipelineDesc->vertexBuffers()->object(XRVideo_VertexInputIndex_vertices)->setMutability(MTL::MutabilityImmutable);
    pipelineDesc->vertexBuffers()->object(XRVideo_VertexInputIndex_instanceData)->setMutability(MTL::MutabilityImmutable);
    pipelineDesc->vertexBuffers()->object(XRVideo_VertexInputIndex_deformationState)->setMutability(MTL::MutabilityImmutable);
    
    pipelineDesc->fragmentBuffers()->object(XRVideo_FragmentInputIndex_instanceData)->setMutability(MTL::MutabilityImmutable);
    
    *pipe = NS::TransferPtr(device->newRenderPipelineState(pipelineDesc.get(), &err));
    if (!*pipe) {
      LOG(ERROR) << "Failed to create render pipeline: " << err->localizedDescription()->utf8String();
      Destroy(); return false;
    }
  }
  
  // Initialize deformation state interpolation
  // TODO: There are plenty of other newComputePipelineState() overloads. Is there a way to specify MTL::MutabilityImmutable on the buffers as we do for the render pipelines to improve performance?
  
  auto interpolateDeformationStateFunction = NS::TransferPtr(library->newFunction(NS::String::string("InterpolateDeformationState", NS::UTF8StringEncoding)));
  interpolateDeformationState = NS::TransferPtr(device->newComputePipelineState(interpolateDeformationStateFunction.get(), &err));
  if (!interpolateDeformationState) {
    LOG(ERROR) << "Failed to create compute pipeline: " << err->localizedDescription()->utf8String();
    Destroy(); return false;
  }
  
  auto interpolateDeformationStateFromIdentityFunction = NS::TransferPtr(library->newFunction(NS::String::string("InterpolateDeformationStateFromIdentity", NS::UTF8StringEncoding)));
  interpolateDeformationStateFromIdentity = NS::TransferPtr(device->newComputePipelineState(interpolateDeformationStateFromIdentityFunction.get(), &err));
  if (!interpolateDeformationStateFromIdentity) {
    LOG(ERROR) << "Failed to create compute pipeline: " << err->localizedDescription()->utf8String();
    Destroy(); return false;
  }
  
  // Initialize depth-stencil state
  auto depthStencilDesc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
  depthStencilDesc->setDepthCompareFunction(usesInverseZ ? MTL::CompareFunctionGreaterEqual : MTL::CompareFunctionLessEqual);
  depthStencilDesc->setDepthWriteEnabled(true);
  depthStencilState = NS::TransferPtr(device->newDepthStencilState(depthStencilDesc.get()));
  
  return true;
}

void MetalXRVideoCommonResources::Destroy() {
  pipeline.reset();
  // normalShadingPipeline.reset();
  interpolateDeformationState.reset();
  interpolateDeformationStateFromIdentity.reset();
}

}

#endif
