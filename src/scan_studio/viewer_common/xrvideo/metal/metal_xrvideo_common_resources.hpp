#pragma once
#ifdef __APPLE__

#include <Metal/Metal.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"  // TODO: Only required for MetalRenderPassDescriptor, which should be in a different file since it has nothing to do with SDL

#include "scan_studio/viewer_common/xrvideo/xrvideo_common_resources.hpp"

namespace vis {
  struct MetalRenderPassDescriptor;
}

namespace scan_studio {
using namespace vis;

class MetalLibraryCache;

/// Initializes and stores common resources that are required for rendering XRVideos,
/// for example, compiled shaders.
class MetalXRVideoCommonResources : public XRVideoCommonResources {
 friend class MetalXRVideo;
 friend class MetalXRVideoFrame;
 friend class MetalXRVideoRenderLock;
 public:
  /// Default constructor, does not initialize the object.
  MetalXRVideoCommonResources() = default;
  
  MetalXRVideoCommonResources(const MetalXRVideoCommonResources& other) = delete;
  MetalXRVideoCommonResources& operator= (const MetalXRVideoCommonResources& other) = delete;
  
  ~MetalXRVideoCommonResources();
  
  bool Initialize(bool outputLinearColors, bool usesInverseZ, MetalRenderPassDescriptor* renderPassDesc, MetalLibraryCache* libraryCache, MTL::Device* device);
  void Destroy();
  
  inline const MetalRenderPassDescriptor& RenderPassDesc() const { return renderPassDesc; }
  inline bool OutputLinearColors() const { return outputLinearColors; }
  inline bool UsesInverseZ() const { return usesInverseZ; }
  
 private:
  // Programs for deformation state interpolation
  NS::SharedPtr<MTL::ComputePipelineState> interpolateDeformationState;
  NS::SharedPtr<MTL::ComputePipelineState> interpolateDeformationStateFromIdentity;
  
  // Pipeline for rendering
  NS::SharedPtr<MTL::RenderPipelineState> pipeline;
  
  // Pipeline for rendering with normal-based shading
  // NS::SharedPtr<MTL::RenderPipelineState> normalShadingPipeline;
  
  // Depth-stencil descriptor
  NS::SharedPtr<MTL::DepthStencilState> depthStencilState;
  
  /// Render pass descriptor with which this MetalXRVideoCommonResources was initialized.
  /// We only keep this here because the Unity plugin cannot be sure about the render pass details
  /// in advance, so it makes an educated guess, but must be able to check this guess against the
  /// actual values later and re-initialize the common resources if the guess was incorrect.
  MetalRenderPassDescriptor renderPassDesc;
  bool outputLinearColors;
  bool usesInverseZ;
};

}

#endif
