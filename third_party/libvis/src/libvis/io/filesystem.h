#pragma once

// Android NDKs before version r22 seem like they might have the <filesystem> header,
// but will fail to link if using it. So, I added some special-case code for this here:
#ifdef __ANDROID__
  #include <android/ndk-version.h>
  #if __NDK_MAJOR__ >= 22
    #define GHC_USE_STD_FS
    #include <filesystem>
    namespace fs {
    using namespace std::filesystem;
    using ifstream = std::ifstream;
    using ofstream = std::ofstream;
    using fstream = std::fstream;
    }
  #else
    //#define GHC_WIN_DISABLE_WSTRING_STORAGE_TYPE
    #define GHC_FILESYSTEM_FWD
    #include <ghc/filesystem.hpp>
    namespace fs {
    using namespace ghc::filesystem;
    using ifstream = ghc::filesystem::ifstream;
    using ofstream = ghc::filesystem::ofstream;
    using fstream = ghc::filesystem::fstream;
    }
  #endif
#else
  // Not building for Android. Use the original logic from fs_std_fwd:
  #include <ghc/fs_std_fwd.hpp>
#endif
