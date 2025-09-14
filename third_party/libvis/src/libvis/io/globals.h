#pragma once

#include <libvis/io/filesystem.h>

/// The application path. If this is set to non-empty, then creating AssetPath and InputStream
/// objects will use this as a base path, treating the given paths as relative to it.
/// This is to make GUI applications independent of the working directory they are started with.
///
/// The implementation of the program that uses libvis is responsible for setting
/// this pointer on startup.
extern fs::path gAppPath;

/// Is gAppPath is non-empty and isRelativeToAppPath is true,
/// prepends gAppPath to the given path and returns the resulting relative path
/// to the result from the current working directory.
/// Otherwise, returns the given path as-is.
inline fs::path MaybePrependAppPath(const fs::path& path, bool isRelativeToAppPath) {
  // On iOS, we use CMake's resource deployment:  
  // https://cmake.org/cmake/help/latest/prop_tgt/RESOURCE.html
  // This places all resources files into the same directory as the executable on iOS.
  // TODO: Since we use this function with (isRelativeToAppPath == true) to access application assets/resources,
  //       this function should be renamed to GetAssetPath() or GetResourcePath(),
  //       with the isRelativeToAppPath parameter removed and instead assumed to be `true`.
  #ifdef __IPHONE_OS_VERSION_MAX_ALLOWED
  if (isRelativeToAppPath) {
    return path.filename();
  }
  #endif
  
  if (isRelativeToAppPath && !gAppPath.empty()) {
    // The reason we use proximate() here is as a protection against potential issues with special
    // characters in file paths. For example, the application may be in some user directory
    // with the user name containing a special character. Then, an absolute path would include
    // this special character. If we don't handle it properly (e.g., convert the fs::path
    // to an ASCII text string), the app will misbehave. But as a relative path, if the target
    // file is also within the directory with the special character, the path will not contain
    // the special character, so we will be fine.
    std::error_code ec;
    return fs::proximate(gAppPath / path, ec);
  } else {
    return path;
  }
}

#ifdef __ANDROID__
class AAssetManager;

/// Since it can be a huge pain to pass Android's AAssetManager pointer around
/// to every place where we want to access Android assets (given that other platforms
/// can simply do standard file accesses that don't require any similar context),
/// we store it in a global pointer.
///
/// The implementation of the program that uses libvis is responsible for setting
/// this pointer on startup.
extern AAssetManager* gAssetManager;
#endif
