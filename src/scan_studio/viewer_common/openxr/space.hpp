#pragma once
#ifdef HAVE_OPENXR

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/openxr/openxr.hpp"
#include "scan_studio/viewer_common/openxr/loader.hpp"

namespace Sophus {
template <class Scalar_, int Options>
class SE3;
using SE3d = SE3<double, 0>;
using SE3f = SE3<float, 0>;
}  // namespace Sophus

namespace scan_studio {
using namespace vis;

// Returns the relative pose estimate between the two spaces A and B at the given time
bool GetEigenPose(XrSpace A, XrSpace B, XrTime time, Sophus::SE3f* A_tr_B);

class OpenXRSpace {
 public:
  /// This does not initialize the object yet. Initialize() must be called for that.
  OpenXRSpace();
  
  OpenXRSpace(const OpenXRSpace& other) = delete;
  OpenXRSpace& operator= (const OpenXRSpace& other) = delete;
  
  OpenXRSpace(OpenXRSpace&& other);
  OpenXRSpace& operator= (OpenXRSpace&& other);
  
  /// Destroys the space.
  ~OpenXRSpace();
  
  /// Attempts to initialize the OpenXR space.
  /// Returns true if successful, false otherwise.
  bool Initialize(XrReferenceSpaceType type, XrSession session, XrPosef poseInReferenceSpace = xrIdentityPose);
  void Destroy();
  
  /// Returns the underlying XrSpace handle.
  inline const XrSpace& GetSpace() const { return space; }
  inline operator XrSpace() const { return space; }
  
 private:
  XrSpace space = XR_NULL_HANDLE;
};

}

#endif
