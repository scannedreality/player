#pragma once

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

namespace scan_studio {
using namespace vis;

/// Base class for XRVideo frames.
class XRVideoFrame {
 public:
  inline XRVideoFrame() {}
  virtual inline ~XRVideoFrame() {}
  
  inline const XRVideoFrameMetadata& GetMetadata() const { return metadata; }
  
 protected:
  /// The frame's metadata
  XRVideoFrameMetadata metadata;
};

}
