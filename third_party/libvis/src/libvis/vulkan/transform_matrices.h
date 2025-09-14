#pragma once

#include <Eigen/Core>

namespace vis {

/// Returns a view matrix for right-handed coordinate systems.
///
/// The source space for the transformation is some right-handed coordinate system.
///
/// The target space for the transformation is a local camera coordinate system with
/// the camera positioned at `eye` in the source space, looking towards `center` in
/// the source space, and with its up direction going towards the `up` direction in
/// the source space. The local camera coordinate system is as follows:
/// +x is right, +y is down, +z is forward in local camera coordinates.
///
/// The up direction does not necessarily need to be at a right angle to the eye-center direction,
/// but of course it should not go into the same direction or be opposite to it.
Eigen::Matrix4f LookAtMatrix(const Eigen::Vector3f& eye, const Eigen::Vector3f& center, const Eigen::Vector3f& up);

/// Returns a perspective projection matrix with the given parameters.
///
/// The source space for the perspective transformation is a local camera coordinate system
/// with +x pointing right, +y pointing down, and +z pointing forward.
///
/// The target space for the perspective transformation is the following view volume:
/// It has a depth range of 0 to 1, and a range of -1 to 1 for x and y.
/// For x and y, (-1, -1) is the upper-left corner of the viewport,
/// and (1, 1) is the bottom-right corner of the viewport.
/// 0 is the minimal depth, and 1 is the maximal depth.
/// This depth range corresponds to that of Vulkan, but not to that of OpenGL (which used -1 to 1).
///
/// Note that as an alternative, libvis' pinhole camera class also has a ToProjectionMatrix() function.
Eigen::Matrix4f PerspectiveMatrix(float verticalFov, float aspectRatio, float zNear, float zFar);

/// Variant of PerspectiveMatrix() for a field-of-view that is not necessarily centered.
///
/// angleLeft and angleDown are negative for a symmetric field-of-view.
/// Input coordinate system: +x is right, +y is up, -z is forward.
/// Output coordinate system: Vulkan's view volume.
///
/// This corresponds to OpenXR's definition of XrFovf:
/// https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#angles
Eigen::Matrix4f PerspectiveMatrixOpenXR(float angleLeft, float angleRight, float angleUp, float angleDown, float zNear, float zFar);

}
