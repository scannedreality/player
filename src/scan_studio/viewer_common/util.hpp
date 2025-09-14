#pragma once

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

/// Safe version of strcpy that works on Windows and Linux (unlike MSVC's strcpy_s()).
/// `destSize` is the size of the destination buffer in bytes.
/// `src` must be NUL-terminated; the NUL must fit into the destination buffer, or else the string will be truncated.
/// Returns true if the string was copied in full, false if it was truncated to fit the destination buffer.
bool SafeStringCopy(char* dest, int destSize, const char* src);

/// Sets the current thread's name.
/// At least with the Linux implementation, the name can only be up to 16 bytes long, including the terminating null character.
#ifdef _WIN32
  void SetThreadNameImpl(const wchar_t* name);
  #define SCAN_STUDIO_SET_THREAD_NAME(name) SetThreadNameImpl(L##name)
#else
  void SetThreadNameImpl(const char* name);
  #define SCAN_STUDIO_SET_THREAD_NAME(name) SetThreadNameImpl(name)
#endif

}
