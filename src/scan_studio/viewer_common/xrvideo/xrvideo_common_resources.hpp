#pragma once

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

/// Initializes and stores common resources that are required for rendering XRVideos,
/// for example, compiled shaders.
class XRVideoCommonResources {
 public:
  /// Default constructor, does not initialize the object.
  XRVideoCommonResources() = default;
  
  XRVideoCommonResources(const XRVideoCommonResources& other) = delete;
  XRVideoCommonResources& operator= (const XRVideoCommonResources& other) = delete;
  
  virtual inline ~XRVideoCommonResources() {}
};

}
