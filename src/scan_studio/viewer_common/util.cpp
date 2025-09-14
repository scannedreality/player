#include "scan_studio/viewer_common/util.hpp"

#include <cstring>

#if defined(__EMSCRIPTEN__)
  #include <emscripten/threading.h>
#elif defined(__linux__)
  #include <sys/prctl.h>
#elif defined(__APPLE__)
  #include <pthread.h>
#elif defined(_WIN32)
  #include <process.h>
  #include <stdlib.h>
  #include <windows.h>
#endif

namespace scan_studio {

bool SafeStringCopy(char* dest, int destSize, const char* src) {
  int srcLen = strlen(src);
  
  if (srcLen + 1 <= destSize) {
    // Copy in full
    memcpy(dest, src, srcLen + 1);
    return true;
  } else {
    // Truncate
    if (destSize > 0) {
      memcpy(dest, src, destSize - 1);
      dest[destSize - 1] = 0;
    }
    return false;
  }
}

#ifdef _WIN32
void SetThreadNameImpl(const wchar_t* name) {
#else
void SetThreadNameImpl(const char* name) {
#endif
  (void) name;
  
  #if defined(__EMSCRIPTEN__)
    // This is a no-op unless the binary is built with --threadprofiler
    emscripten_set_thread_name(pthread_self(), name);
  #elif defined(__linux__)
    prctl(PR_SET_NAME, name);
  #elif defined(__APPLE__)
    pthread_setname_np(name);
  #elif defined(_WIN32)
    struct Initializer {
      inline Initializer() {
        #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
          HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
          if (kernel32) {
            setThreadDescription = reinterpret_cast<decltype(setThreadDescription)>(GetProcAddress(kernel32, "SetThreadDescription"));
          }
        #endif
      }
      HRESULT (WINAPI* setThreadDescription)(HANDLE, PCWSTR) = nullptr;
    };
    
    // The constructor of this static object is thread-safe, see:
    // https://stackoverflow.com/questions/8102125/is-local-static-variable-initialization-thread-safe-in-c11
    static Initializer initializer;
    
    if (initializer.setThreadDescription) {
      initializer.setThreadDescription(GetCurrentThread(), name);
    }
  #endif
}

}
