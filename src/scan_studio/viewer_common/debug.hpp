#pragma once

#include <fstream>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

/// For a description of the format, see: https://en.wikipedia.org/wiki/Netpbm
inline bool SaveDebugImageAsPPM(const char* path, int width, int height, bool inColor, const u8* data) {
  ofstream file(path, ios::out | ios::binary | ios::trunc);
  if (!file.is_open()) { return false; }
  
  file << (inColor ? "P6" : "P5") << endl  // P6 stands for binary RGB format, P5 for binary grayscale
       << width << " " << height << endl
       << "255" << endl;
  
  file.write(reinterpret_cast<const char*>(data), width * height * (inColor ? 3 : 1));
  
  return true;
}

}
