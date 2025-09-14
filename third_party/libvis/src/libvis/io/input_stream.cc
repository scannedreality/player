#include "libvis/io/input_stream.h"

#include <cstring>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

#include "libvis/io/asset_path.h"
#include "libvis/io/globals.h"

namespace vis {

bool InputStream::ReadAll(vector<u8>* out) {
  out->resize(SizeInBytes());
  return Seek(0) && Read(out->data(), out->size()) == out->size();
}


IfstreamInputStream::IfstreamInputStream(IfstreamInputStream&& other)
    : file(std::move(other.file)) {}

IfstreamInputStream& IfstreamInputStream::operator=(IfstreamInputStream&& other) {
  file = std::move(other.file);
  return *this;
}

bool IfstreamInputStream::Open(const fs::path& path, bool isRelativeToAppPath) {
  Close();
  file.open(MaybePrependAppPath(path, isRelativeToAppPath).string(), ios::in | ios::binary);
  return file.is_open();
}

void IfstreamInputStream::Close() {
  file.close();
}

usize IfstreamInputStream::Read(void* data, usize size) {
  file.read(reinterpret_cast<char*>(data), size);
  return file.gcount();
}

bool IfstreamInputStream::Seek(u64 offsetFromStart) {
  file.clear();  // clear error state flags
  file.seekg(offsetFromStart);
  return !file.fail();
}

u64 IfstreamInputStream::SizeInBytes() {
  const u64 originalPos = file.tellg();
  file.seekg(0, std::ios::end);
  const u64 size = file.tellg();
  file.seekg(originalPos, std::ios::beg);
  return size;
}


#ifdef __ANDROID__
AAssetInputStream::AAssetInputStream(AAssetInputStream&& other)
    : assetManager(other.assetManager),
      asset(other.asset) {
  other.asset = nullptr;
}

AAssetInputStream& AAssetInputStream::operator=(AAssetInputStream&& other) {
  Close();
  assetManager = other.assetManager;
  asset = other.asset;
  other.asset = nullptr;
  return *this;
}

AAssetInputStream::~AAssetInputStream() {
  Close();
}

bool AAssetInputStream::Open(const fs::path& path, bool isRelativeToAppPath, AAssetManager* assetManager) {
  Close();
  
  this->assetManager = assetManager;
  
  asset = AAssetManager_open(assetManager, MaybePrependAppPath(path, isRelativeToAppPath).string().c_str(), AASSET_MODE_BUFFER);
  return asset != nullptr;
}

void AAssetInputStream::Close() {
  if (asset != nullptr) {
    AAsset_close(asset);
    asset = nullptr;
  }
}

usize AAssetInputStream::Read(void* data, usize size) {
  return AAsset_read(asset, data, size);
}

bool AAssetInputStream::Seek(u64 fileOffset) {
  return AAsset_seek64(asset, fileOffset, SEEK_SET) == fileOffset;
}

u64 AAssetInputStream::SizeInBytes() {
  return AAsset_getLength64(asset);
}
#endif


InputStream* OpenAsset(const fs::path& path, bool isRelativeToAppPath) {
  #ifdef __ANDROID__
    AAssetInputStream* result = new AAssetInputStream();
    if (result->Open(path, isRelativeToAppPath, gAssetManager)) {
      return result;
    } else {
      delete result;
      return nullptr;
    }
  #else
    IfstreamInputStream* result = new IfstreamInputStream();
    if (result->Open(path, isRelativeToAppPath)) {
      return result;
    } else {
      delete result;
      return nullptr;
    }
  #endif
}

unique_ptr<InputStream> OpenAssetUnique(const fs::path& path, bool isRelativeToAppPath) {
  return unique_ptr<InputStream>(OpenAsset(path, isRelativeToAppPath));
}


MemoryInputStream::MemoryInputStream(MemoryInputStream&& other)
    : baseAddress(other.baseAddress),
      readPtr(other.readPtr),
      size(other.size) {}

MemoryInputStream& MemoryInputStream::operator=(MemoryInputStream&& other) {
  baseAddress = other.baseAddress;
  readPtr = other.readPtr;
  size = other.size;
  return *this;
}

void MemoryInputStream::SetSource(const void* source, usize size) {
  baseAddress = source;
  readPtr = source;
  this->size = size;
}

usize MemoryInputStream::Read(void* data, usize size) {
  const usize readableSize = min<usize>(size, this->size - static_cast<uintptr_t>(static_cast<const u8*>(readPtr) - static_cast<const u8*>(baseAddress)));
  memcpy(data, readPtr, readableSize);
  readPtr = static_cast<const void*>(static_cast<const u8*>(readPtr) + readableSize);
  return readableSize;
}

bool MemoryInputStream::Seek(u64 offsetFromStart) {
  if (offsetFromStart > size) { return false; }
  readPtr = static_cast<const void*>(static_cast<const u8*>(baseAddress) + offsetFromStart);
  return true;
}

u64 MemoryInputStream::SizeInBytes() {
  return size;
}


VectorInputStream::VectorInputStream(vector<u8>&& source) {
  SetSource(std::move(source));
}

VectorInputStream::VectorInputStream(VectorInputStream&& other)
    : data(std::move(other.data)),
      readPtr(other.readPtr) {}

VectorInputStream& VectorInputStream::operator=(VectorInputStream&& other) {
  data = std::move(other.data);
  readPtr = other.readPtr;
  return *this;
}

void VectorInputStream::SetSource(vector<u8>&& source) {
  data = std::move(source);
  readPtr = data.data();
}

usize VectorInputStream::Read(void* data, usize size) {
  const usize readableSize = min<usize>(size, this->data.size() - static_cast<uintptr_t>(static_cast<const u8*>(readPtr) - static_cast<const u8*>(this->data.data())));
  memcpy(data, readPtr, readableSize);
  readPtr = static_cast<const void*>(static_cast<const u8*>(readPtr) + readableSize);
  return readableSize;
}

bool VectorInputStream::Seek(u64 offsetFromStart) {
  if (offsetFromStart > data.size()) { return false; }
  readPtr = static_cast<const void*>(data.data() + offsetFromStart);
  return true;
}

u64 VectorInputStream::SizeInBytes() {
  return data.size();
}

}
