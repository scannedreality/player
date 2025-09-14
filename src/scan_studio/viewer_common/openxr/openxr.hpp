#pragma once
#ifdef HAVE_OPENXR

#if defined(_WIN32)
  #define XR_USE_PLATFORM_WIN32
  #include <Windows.h>
  #include <unknwn.h>
#elif defined(__ANDROID__)
  #define XR_USE_PLATFORM_ANDROID
  #include <android/native_window_jni.h>
#else
  // TODO: Other options:
  // XR_USE_PLATFORM_XLIB
  // XR_USE_PLATFORM_XCBX
  // XR_USE_PLATFORM_WAYLAND
#endif

#define XR_USE_GRAPHICS_API_VULKAN

#include <string>
#include <vector>

#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/volk.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace scan_studio {
using namespace vis;

constexpr XrPosef xrIdentityPose = {{0, 0, 0, 1}, {0, 0, 0}};

/// Returns true if the result is XR_SUCCESS.
/// Otherwise, prints the error code name and returns false.
bool XrCheckResult(XrResult result);

// This uses a macro such that LOG(ERROR) picks up the correct file and line number.
#define XR_CHECKED_CALL(xr_call)                         \
  do {                                                   \
    XrResult result = (xr_call);                         \
    if (!XrCheckResult(result)) {                        \
      LOG(ERROR) << "OpenXR Error in XR_CHECKED_CALL";   \
    }                                                    \
  } while(false)

}

#endif
