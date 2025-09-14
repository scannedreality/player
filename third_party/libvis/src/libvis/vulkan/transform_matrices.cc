#include "libvis/vulkan/transform_matrices.h"

#include <Eigen/Geometry>

namespace vis {

Eigen::Matrix4f LookAtMatrix(const Eigen::Vector3f& eye, const Eigen::Vector3f& center, const Eigen::Vector3f& up) {
  const Eigen::Vector3f f = (center - eye).normalized();  // forward
  const Eigen::Vector3f s = f.cross(up).normalized();  // side (right)
  const Eigen::Vector3f u = s.cross(f);  // up (guaranteed to be at a right angle to forward and side)
  
  Eigen::Matrix4f result;
  
  result(0, 0) = s.x();
  result(0, 1) = s.y();
  result(0, 2) = s.z();
  result(1, 0) = -u.x();
  result(1, 1) = -u.y();
  result(1, 2) = -u.z();
  result(2, 0) = f.x();
  result(2, 1) = f.y();
  result(2, 2) = f.z();
  
  result(0, 3) = -s.dot(eye);
  result(1, 3) = u.dot(eye);
  result(2, 3) = -f.dot(eye);
  
  result(3, 0) = 0;
  result(3, 1) = 0;
  result(3, 2) = 0;
  result(3, 3) = 1;
  
  return result;
}

Eigen::Matrix4f PerspectiveMatrix(float verticalFov, float aspectRatio, float zNear, float zFar) {
  float const tanHalfFovy = tan(verticalFov / 2.f);
  
  Eigen::Matrix4f result;
  result.setZero();
  
  // X scaling
  result(0, 0) = 1.f / (aspectRatio * tanHalfFovy);
  
  // Y scaling
  result(1, 1) = 1.f / (tanHalfFovy);
  
  // This gets the resulting (X, Y, Z) divided by Z
  result(3, 2) = 1.f;
  
  // This sets the resulting Z such that it ranges from 0 to 1 after division by result.W = input.Z
  result(2, 2) = -zFar / (zNear - zFar);
  result(2, 3) = -(zFar * zNear) / (zFar - zNear);
  
  return result;
}

Eigen::Matrix4f PerspectiveMatrixOpenXR(float angleLeft, float angleRight, float angleUp, float angleDown, float zNear, float zFar) {
  float const tanAngleLeft = tan(angleLeft);
  float const tanAngleRight = tan(angleRight);
  float const tanAngleUp = tan(angleUp);
  float const tanAngleDown = tan(angleDown);
  
  Eigen::Matrix4f result;
  result.setZero();
  
  // With the first row of the desired matrix being:
  // a 0 b 0
  // (Note that b is in the 3rd instead of 4th column since it must be independent from z,
  //  which we achieve by having it multiplied with z as part of the matrix-vector multiplication,
  //  and then divided by z again since we set the 4th element of the result vector to -z.)
  //
  // We would like to have:
  // a * x + b == -1,  for x == tanAngleLeft
  // a * x + b ==  1,  for x == tanAngleRight
  //
  // Thus:
  // a * tanAngleLeft + b == -1
  // a * tanAngleRight + b == 1
  //
  // b == 1 - a * tanAngleRight
  //
  // a * tanAngleLeft + 1 - a * tanAngleRight == -1
  // a * tanAngleLeft - a * tanAngleRight == -2
  // a * (tanAngleLeft - tanAngleRight) == -2
  // a == -2 / (tanAngleLeft - tanAngleRight)
  // ==> a == 2 / (tanAngleRight - tanAngleLeft)
  //
  // ==> b == 1 - (2 * tanAngleRight) / (tanAngleRight - tanAngleLeft)
  //
  // In addition, it must be taken into account that b gets multiplied with z,
  // which is negative in the input coordinate system convention used here.
  // The 4th row causes a division by -z, but that leaves a -1 that has to be multiplied onto b.
  
  // X scaling and offset
  result(0, 0) = 2.f / (tanAngleRight - tanAngleLeft);
  result(0, 2) = -1 * (1.f - (2.f * tanAngleRight) / (tanAngleRight - tanAngleLeft));
  
  // Y scaling and offset
  result(1, 1) = -1 * (2.f / (tanAngleUp - tanAngleDown));
  result(1, 2) = 1.f - (2.f * tanAngleUp) / (tanAngleUp - tanAngleDown);
  
  // This gets the resulting (X, Y, Z) divided by -Z
  result(3, 2) = -1.f;
  
  // This sets the resulting Z such that it ranges from 0 to 1 after division by result.W = input.Z
  result(2, 2) = zFar / (zNear - zFar);
  result(2, 3) = -(zFar * zNear) / (zFar - zNear);
  
  return result;
}

}
