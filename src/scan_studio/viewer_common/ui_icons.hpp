#pragma once

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

// Contains icon definitions used in the viewer UI to make them re-usable for other / embedded UIs.
// Vertex coordinates are in the range [0, 1] and should be scaled to the desired icon size.

// Info icon [i]
extern array<Eigen::Vector2f, 12> infoIconVertices;
extern array<u16, 3 * 10> infoIconIndices;

// Back icon <-
extern array<Eigen::Vector2f, 9> backIconVertices;
extern array<u16, 3 * 7> backIconIndices;

// Pause icon ||
extern array<Eigen::Vector2f, 8> pauseIconVertices;
extern array<u16, 3 * 4> pauseIconIndices;

// Resume icon >
extern array<Eigen::Vector2f, 3> resumeIconVertices;
extern array<u16, 3> resumeIconIndices;

// Repeat icon O
struct RepeatIcon {
  /// Computes the icon vertices and indices
  RepeatIcon();
  
  /// Returns a singleton instance (to avoid duplicate icon computation)
  static RepeatIcon& Instance();
  
  static constexpr int kSegments = 32;
  
  array<Eigen::Vector2f, 2 * kSegments + 3> vertices;
  array<u16, 2 * 3 * (kSegments - 1) + 3> indices;
};

}
