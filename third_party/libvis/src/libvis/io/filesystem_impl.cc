// Android NDKs before version r22 seem like they might have the <filesystem> header,
// but will fail to link if using it. So, I added some special-case code for this here:
#ifdef __ANDROID__
  #include <android/ndk-version.h>
  #if __NDK_MAJOR__ >= 22
    #define GHC_USE_STD_FS
  #else
    //#define GHC_WIN_DISABLE_WSTRING_STORAGE_TYPE
    #define GHC_FILESYSTEM_IMPLEMENTATION
    #include <ghc/filesystem.hpp>
  #endif
#else
  // Not building for Android. Use the original logic from fs_std_impl:
  #include <ghc/fs_std_impl.hpp>
#endif

#include <libvis/io/filesystem.h>
