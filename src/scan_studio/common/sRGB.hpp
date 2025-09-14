#pragma once

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

/// Converts the given sRGB color value to a linear color value.
/// The input and output range for each color component is [0, 1].
inline Eigen::Vector3f SRGBToLinear(const Eigen::Vector3f& input) {
  Eigen::Vector3f result;
  for (int c = 0; c < 3; ++ c) {
    if (input[c] <= 0.04045f) {
      result[c] = input[c] / 12.92f;
    } else {
      result[c] = powf((input[c] + 0.055f) / 1.055f, 2.4f);
    }
  }
  return result;
}

/// Converts the given sRGBA color value to a linear color value.
/// The input and output range for each color component is [0, 1].
/// Does not modify the alpha value.
inline Eigen::Vector4f SRGBToLinear(const Eigen::Vector4f& input) {
  Eigen::Vector4f result;
  for (int c = 0; c < 3; ++ c) {
    if (input[c] <= 0.04045f) {
      result[c] = input[c] / 12.92f;
    } else {
      result[c] = powf((input[c] + 0.055f) / 1.055f, 2.4f);
    }
  }
  result[3] = input[3];
  return result;
}

/// Converts the given linear color value to an sRGB color value.
/// The input and output range for each color component is [0, 1].
inline Eigen::Vector3f LinearToSRGB(const Eigen::Vector3f& input) {
  Eigen::Vector3f result;
  for (int c = 0; c < 3; ++ c) {
    if (input[c] <= 0.0031308f) {
      result[c] = input[c] * 12.92f;
    } else {
      result[c] = 1.055f * powf(input[c], 1 / 2.4f) - 0.055f;
    }
  }
  return result;
}

/// Converts the given linear color value to an sRGBA color value.
/// The input and output range for each color component is [0, 1].
/// Does not modify the alpha value.
inline Eigen::Vector4f LinearToSRGB(const Eigen::Vector4f& input) {
  Eigen::Vector4f result;
  for (int c = 0; c < 3; ++ c) {
    if (input[c] <= 0.0031308f) {
      result[c] = input[c] * 12.92f;
    } else {
      result[c] = 1.055f * powf(input[c], 1 / 2.4f) - 0.055f;
    }
  }
  result[3] = input[3];
  return result;
}

}
