#include "scan_studio/viewer_common/license_texts.hpp"

#include "scan_studio/viewer_common/license_texts/texts.hpp"

namespace scan_studio {

OpenSourceComponent openSourceComponents[openSourceComponentCount] = {
    #ifdef HAVE_VULKAN
      {"base64 (C++)", &base64},
    #endif
    {"dav1d", &dav1d},
    {"DroidSans font", &droidsans},
    {"Eigen", &eigen},
    {"Flexible and Economical UTF-8 Decoder", &utf8decoder},
    {"filesystem", &::filesystem},
    {"fontstash", &fontstash},
    #ifdef HAVE_VULKAN
      {"gli", &gli},
    #endif
    #ifndef __EMSCRIPTEN__
      {"libavif", &libavif},
    #endif
    {"loguru", &loguru},
    #ifdef __APPLE__
      {"metal-cpp", &metalcpp},
    #endif
    #ifdef HAVE_VULKAN
      {"nlohmann/json", &nlohmannjson},
      {"OpenXR SDK", &openxrsdk},
    #endif
    {"Simple DirectMedia Layer (SDL) Version 2.0", &sdl},
    {"Sophus", &sophus},
    #ifdef HAVE_VULKAN
      {"TinyGLTF", &tinygltf},
      {"volk", &volk},
      {"Vulkan-glTF-PBR", &vulkangltfpbr},
      {"VulkanMemoryAllocator (VMA)", &vulkanmemoryallocator},
    #endif
    #ifdef __EMSCRIPTEN__
      {"wasm-feature-detect", &wasmfeaturedetect},
    #endif
    {"Zstandard (zstd)", &zstd}};

}
