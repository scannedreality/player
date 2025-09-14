#pragma once

#include <string>
#include <vector>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

struct OpenSourceComponent {
  string name;
  vector<string>* licenseLines;
};

constexpr int openSourceComponentCount =
    10  // basic licenses used in every build
    #ifdef __EMSCRIPTEN__
      + 1  // wasm-feature-detect
    #else
      + 1  // libavif
    #endif
    #ifdef HAVE_VULKAN
      + 8
    #endif
    #ifdef __APPLE__
      + 1  // metal-cpp
    #endif
    ;

extern OpenSourceComponent openSourceComponents[openSourceComponentCount];

}
