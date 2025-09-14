#pragma once
#ifdef HAVE_D3D11

#include <memory>

#include <d3d11_4.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/xrvideo_common_resources.hpp"

namespace scan_studio {
using namespace vis;

/// Initializes and stores common resources that are required for rendering XRVideos,
/// for example, compiled shaders.
class D3D11XRVideoCommonResources : public XRVideoCommonResources {
 friend class D3D11XRVideo;
 friend class D3D11XRVideoFrame;
 friend class D3D11XRVideoRenderLock;
 public:
  /// Default constructor, does not initialize the object.
  D3D11XRVideoCommonResources() = default;
  
  D3D11XRVideoCommonResources(const D3D11XRVideoCommonResources& other) = delete;
  D3D11XRVideoCommonResources& operator= (const D3D11XRVideoCommonResources& other) = delete;
  
  ~D3D11XRVideoCommonResources();
  
  bool Initialize(ID3D11Device5* device);
  void Destroy();
  
 private:
  // Programs for deformation state interpolation
  shared_ptr<ID3D11ComputeShader> interpolateDeformationState;
  shared_ptr<ID3D11ComputeShader> interpolateDeformationStateFromIdentity;
};

}

#endif
