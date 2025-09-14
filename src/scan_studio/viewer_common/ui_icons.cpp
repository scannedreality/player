#include "scan_studio/viewer_common/ui_icons.hpp"

#include "scan_studio/viewer_common/pi.hpp"

namespace scan_studio {

array<Eigen::Vector2f, 12> infoIconVertices = {
    Eigen::Vector2f(0.18f, 0.18f),          // [0] top left box corner
    Eigen::Vector2f(1 - 0.18f, 0.18f),      // [1] top right box corner
    Eigen::Vector2f(0.18f, 1 - 0.18f),      // [2] bottom left box corner
    Eigen::Vector2f(1 - 0.18f, 1 - 0.18f),  // [3] bottom right box corner
    Eigen::Vector2f(0.444f, 0.254f),        // [4] top left 'i' dot corner
    Eigen::Vector2f(1 - 0.444f, 0.254f),    // [5] top right 'i' dot corner
    Eigen::Vector2f(0.444f, 0.366f),        // [6] bottom left 'i' dot corner
    Eigen::Vector2f(1 - 0.444f, 0.366f),    // [7] bottom right 'i' dot corner
    Eigen::Vector2f(0.444f, 0.458f),        // [8] top left 'i' body corner
    Eigen::Vector2f(1 - 0.444f, 0.458f),    // [9] top right 'i' body corner
    Eigen::Vector2f(0.444f, 0.746f),        // [10] bottom left 'i' body corner
    Eigen::Vector2f(1 - 0.444f, 0.746f)};   // [11] bottom right 'i' body corner

array<u16, 3 * 10> infoIconIndices = {
      0, 4, 1,  // top 1
      1, 4, 5,  // top 2
      0, 2, 4,  // left 1
      4, 2, 10,  // left 2
      2, 11, 10,  // bottom 1
      11, 2, 3,  // bottom 2
      1, 5, 3,  // right 1
      5, 11, 3,  // right 2
      6, 8, 7,  // in-between the 'i' dot and body 1
      7, 8, 9};  // in-between the 'i' dot and body 2


array<Eigen::Vector2f, 9> backIconVertices = {
    Eigen::Vector2f(0.189f, 0.500f),       // [0] tip of arrow
    Eigen::Vector2f(0.464f, 0.225f),       // [1] topmost point of arrow shape
    Eigen::Vector2f(0.535f, 0.296f),       // [2] to the right of the topmost point
    Eigen::Vector2f(0.382f, 0.450f),       // [3] top inset point
    Eigen::Vector2f(0.772f, 0.450f),       // [4] top side of arrow end (on the right)
    Eigen::Vector2f(0.772f, 1 - 0.450f),   // [5] bottom side of arrow end (on the right)
    Eigen::Vector2f(0.382f, 1 - 0.450f),   // [6] bottom inset point
    Eigen::Vector2f(0.535f, 1 - 0.296f),   // [7] to the right of the bottommost point
    Eigen::Vector2f(0.464f, 1 - 0.225f)};  // [8] bottommost point of arrow shape

array<u16, 3 * 7> backIconIndices = {
      0, 2, 1,   // 1/2 of top part
      0, 3, 2,   // 1/2 of top part
      0, 4, 3,   // top of arrow trunk
      0, 5, 4,   // center of arrow trunk
      0, 6, 5,   // bottom of arrow trunk
      0, 7, 6,   // 1/2 of bottom part
      0, 8, 7};  // 1/2 of bottom part


array<Eigen::Vector2f, 8> pauseIconVertices = {
    Eigen::Vector2f(0.233f, 0.169f),
    Eigen::Vector2f(0.393f, 0.169f),
    Eigen::Vector2f(0.393f, 0.831f),
    Eigen::Vector2f(0.233f, 0.831f),
    Eigen::Vector2f(0.607f, 0.169f),
    Eigen::Vector2f(0.767f, 0.169f),
    Eigen::Vector2f(0.767f, 0.831f),
    Eigen::Vector2f(0.607f, 0.831f)};
array<u16, 3 * 4> pauseIconIndices = {
    0, 3, 1,
    1, 3, 2,
    4, 7, 5,
    5, 7, 6};


array<Eigen::Vector2f, 3> resumeIconVertices = {
    Eigen::Vector2f(0.241f, 0.168f),
    Eigen::Vector2f(0.241f, 0.832f),
    Eigen::Vector2f(0.792f, 0.500f)};
array<u16, 3> resumeIconIndices = {0, 1, 2};


RepeatIcon::RepeatIcon() {
  // Create the 3/4 ring
  constexpr float kAngleRange = 3.f / 4.f * (2 * M_PI);
  
  const Eigen::Vector2f center = Eigen::Vector2f(0.5f, 0.5f);
  
  const float innerRadius = 0.5f * 0.388f;
  const float outerRadius = 0.5f * 0.575f;
  
  for (int segment = 0; segment < kSegments; ++ segment) {
    const float angle = -1.f / 4.f * (2 * M_PI) + kAngleRange * (segment / (kSegments - 1.f));
    const Eigen::Vector2f direction(sinf(angle), cosf(angle));
    
    const int baseVertex = 2 * segment;
    vertices[baseVertex + 0] = center + innerRadius * direction;
    vertices[baseVertex + 1] = center + outerRadius * direction;
    
    if (segment < kSegments - 1) {
      const int baseIndex = 2 * 3 * segment;
      indices[baseIndex + 0] = baseVertex + 0;
      indices[baseIndex + 1] = baseVertex + 1;
      indices[baseIndex + 2] = baseVertex + 2;
      
      indices[baseIndex + 3] = baseVertex + 2;
      indices[baseIndex + 4] = baseVertex + 1;
      indices[baseIndex + 5] = baseVertex + 3;
    }
  }
  
  // Create the triangle tip
  vertices[vertices.size() - 3] = Eigen::Vector2f(0.304f, 0.083f + 0.5f * 0.362f);  // left (middle)
  vertices[vertices.size() - 2] = Eigen::Vector2f(0.304f + 0.196f, 0.083f);  // top right (side)
  vertices[vertices.size() - 1] = Eigen::Vector2f(0.304f + 0.196f, 0.083f + 0.362f);  // bottom right (side)
  indices[indices.size() - 3] = vertices.size() - 3;
  indices[indices.size() - 2] = vertices.size() - 1;
  indices[indices.size() - 1] = vertices.size() - 2;
}

RepeatIcon& RepeatIcon::Instance() {
  static RepeatIcon instance;
  return instance;
}

}
