#pragma once

#include <libvis/io/filesystem.h>
#include <memory>

#ifdef __ANDROID__
class AAssetManager;
#endif

#include "libvis/vulkan/libvis.h"

namespace vis {
using namespace std;

class InputStream;

/// Abstracts a base path from where to read assets, such that we can read assets from FILE* as well as from Android assets.
/// A simple string respectively fs::path is not sufficient for this since for Android assets, we need the AAssetManager pointer.
/// (It would be sufficient in case we made the AAssetManager pointer global.)
/// TODO: We now have a global AAssetManager pointer. It seems like the most clean way forward to delete the AssetPath class and use a simple fs::path instead.
class AssetPath {
 public:
  AssetPath(const fs::path& basePath, bool isRelativeToAppPath);
  virtual inline ~AssetPath() {}
  
  /// Static factory function that returns an AAssetAssetPath or a FileAssetPath depending on whether the platform is Android or not.
  /// For Android, uses the global asset manager pointer gAssetManager.
  /// The returned object must be deleted with delete.
  static AssetPath* Create(const fs::path& basePath, bool isRelativeToAppPath);
  static unique_ptr<AssetPath> CreateUnique(const fs::path& basePath, bool isRelativeToAppPath);
  
  /// Opens the file at the given sub-path relative to this asset path, returning an open InputStream.
  /// The returned object must be deleted with delete. If the file cannot be opened, returns nullptr.
  InputStream* Open(const char* path);
  InputStream* Open(const fs::path& path);
  
  /// See Open(). Returns a unique_ptr instead of a raw pointer.
  unique_ptr<InputStream> OpenUnique(const char* path);
  unique_ptr<InputStream> OpenUnique(const fs::path& path);
  
  inline const fs::path& BasePath() const { return basePath; }
  
 protected:
  virtual InputStream* OpenImpl(const fs::path& path) = 0;
  
  fs::path basePath;
};

/// AssetPath implementation for files.
class FileAssetPath : public AssetPath {
 public:
  explicit FileAssetPath(const fs::path& basePath, bool isRelativeToAppPath);
  
 protected:
  virtual InputStream* OpenImpl(const fs::path& path) override;
};

#ifdef __ANDROID__
/// AssetPath implementation for Android assets.
class AAssetAssetPath : public AssetPath {
 public:
  AAssetAssetPath(const fs::path& basePath, bool isRelativeToAppPath, AAssetManager* assetManager);
  
 protected:
  virtual InputStream* OpenImpl(const fs::path& path) override;
  
 private:
  AAssetManager* assetManager;
};
#endif

}
