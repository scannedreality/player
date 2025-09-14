#include "libvis/io/asset_path.h"

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

#include "libvis/io/globals.h"
#include "libvis/io/input_stream.h"

namespace vis {

AssetPath::AssetPath(const fs::path& basePath, bool isRelativeToAppPath) {
  this->basePath = MaybePrependAppPath(basePath, isRelativeToAppPath);
}

AssetPath* AssetPath::Create(const fs::path& basePath, bool isRelativeToAppPath) {
  #ifdef __ANDROID__
    return new AAssetAssetPath(basePath, isRelativeToAppPath, gAssetManager);
  #else
    return new FileAssetPath(basePath, isRelativeToAppPath);
  #endif
}

unique_ptr<AssetPath> AssetPath::CreateUnique(const fs::path& basePath, bool isRelativeToAppPath) {
  return unique_ptr<AssetPath>(Create(basePath, isRelativeToAppPath));
}

InputStream* AssetPath::Open(const char* path) {
  return OpenImpl(basePath / path);
}

InputStream* AssetPath::Open(const fs::path& path) {
  return OpenImpl(basePath / path);
}

unique_ptr<InputStream> AssetPath::OpenUnique(const char* path) {
  return unique_ptr<InputStream>(Open(path));
}

unique_ptr<InputStream> AssetPath::OpenUnique(const fs::path& path) {
  return unique_ptr<InputStream>(Open(path));
}


FileAssetPath::FileAssetPath(const fs::path& basePath, bool isRelativeToAppPath)
    : AssetPath(basePath, isRelativeToAppPath) {}

InputStream* FileAssetPath::OpenImpl(const fs::path& path) {
  IfstreamInputStream* file = new IfstreamInputStream();
  if (!file->Open(path, /*isRelativeToAppPath*/ false)) {
    delete file;
    file = nullptr;
  }
  return file;
}


#ifdef __ANDROID__
AAssetAssetPath::AAssetAssetPath(const fs::path& basePath, bool isRelativeToAppPath, AAssetManager* assetManager)
    : AssetPath(basePath, isRelativeToAppPath),
      assetManager(assetManager) {}

InputStream* AAssetAssetPath::OpenImpl(const fs::path& path) {
  AAssetInputStream* file = new AAssetInputStream();
  if (!file->Open(path, /*isRelativeToAppPath*/ false, assetManager)) {
    delete file;
    file = nullptr;
  }
  return file;
}
#endif

}
