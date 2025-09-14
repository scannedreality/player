#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/openxr/space.hpp"

#include "scan_studio/viewer_common/pi.hpp"
#include <sophus/se3.hpp>

namespace scan_studio {

bool GetEigenPose(XrSpace A, XrSpace B, XrTime time, Sophus::SE3f* A_tr_B) {
  XrSpaceLocation spaceLocation = {XR_TYPE_SPACE_LOCATION};
  XrResult res = xrLocateSpace(B, A, time, &spaceLocation);
  if (XR_UNQUALIFIED_SUCCESS(res) &&
      (spaceLocation.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) == (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
    *A_tr_B = Sophus::SE3f(Eigen::Quaternionf(spaceLocation.pose.orientation.w, spaceLocation.pose.orientation.x, spaceLocation.pose.orientation.y, spaceLocation.pose.orientation.z),
                           Eigen::Vector3f(spaceLocation.pose.position.x, spaceLocation.pose.position.y, spaceLocation.pose.position.z));
    return true;
  }
  return false;
};


OpenXRSpace::OpenXRSpace() {}

OpenXRSpace::OpenXRSpace(OpenXRSpace&& other)
    : space(move(other.space)) {
  other.space = XR_NULL_HANDLE;
}

OpenXRSpace& OpenXRSpace::operator=(OpenXRSpace&& other) {
  Destroy();
  
  space = move(other.space);
  other.space = XR_NULL_HANDLE;
  
  return *this;
}

OpenXRSpace::~OpenXRSpace() {
  Destroy();
}

bool OpenXRSpace::Initialize(XrReferenceSpaceType type, XrSession session, XrPosef poseInReferenceSpace) {
  XrReferenceSpaceCreateInfo refSpaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  refSpaceInfo.poseInReferenceSpace = poseInReferenceSpace;
  refSpaceInfo.referenceSpaceType = type;
  
  if (!XrCheckResult(xrCreateReferenceSpace(session, &refSpaceInfo, &space))) {
    return false;
  }
  
  return true;
}

void OpenXRSpace::Destroy() {
  if (space != XR_NULL_HANDLE) {
    xrDestroySpace(space);
    space = XR_NULL_HANDLE;
  }
}

}

#endif
