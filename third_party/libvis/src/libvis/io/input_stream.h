#pragma once

#include <libvis/io/filesystem.h>
#include <fstream>
#include <vector>

#ifdef __ANDROID__
class AAsset;
class AAssetManager;
#endif

#include "libvis/vulkan/libvis.h"

namespace vis {
using namespace std;

class AssetPath;

/// Virtual base class for input streams.
///
/// By using this class for data input, one can generically support
/// reading data from different sources such as files, memory, sockets, or Android assets,
/// instead of hard-coding a specific type, or re-implementing input from each of them.
///
/// The class implementing the generic input should simply take a pointer to a
/// InputStream instance and call its Read() (and optionally, Seek(), ReadAll(), or SizeInBytes())
/// functions. The user of the class should be responsible for creating the
/// input stream (and if neccessary, opening the file, for example).
class InputStream {
 public:
  virtual inline ~InputStream() {}
  
  /// Reads the given number of bytes into the buffer, if available.
  /// If less bytes are available, reads that number of bytes.
  /// Returns the number of bytes read.
  virtual usize Read(void* data, usize size) = 0;
  
  /// Helper that calls Read() and returns true only if all of the
  /// requested number of bytes were read successfully.
  inline bool ReadFully(void* data, usize size) {
    return Read(data, size) == size;
  }
  
  /// Reads the whole stream content into a vector<u8>.
  /// (TODO: An implementation for sockets should parse some kind of message header
  ///        defined by the networking protocol and then limit the bytes that it allows the InputStream
  ///        to read to the size of the network message (and also recv() the rest if the InputStream does not
  ///        read all of the message); so this should be supported on sockets, just as SizeInBytes(), but not Seek().)
  /// Returns true on success, false on failure.
  bool ReadAll(vector<u8>* out);
  
  /// If the derived class implements this function (which is optional),
  /// calling it from one thread aborts a Read() call that may be in progress by another thread.
  /// This is intended for streams that receive data from the network, allowing to abort reads if they
  /// do not finish due to a network connection being down or very slow.
  /// If not implemented by the derived class, calling this function does nothing.
  virtual void AbortRead() {}
  
  /// Seeks to the given read offset, measured in bytes from the start of the input stream.
  /// Attention: Note that sockets do not support this, so this must be avoided if operating on sockets.
  /// Returns true on success, false on failure.
  virtual bool Seek(u64 offsetFromStart) = 0;
  
  /// Returns the size of the whole stream content in bytes.
  virtual u64 SizeInBytes() = 0;
};

/// Implementation of InputStream for ifstream objects.
class IfstreamInputStream : public InputStream {
 public:
  inline IfstreamInputStream() {}
  
  IfstreamInputStream(const IfstreamInputStream& other) = delete;
  IfstreamInputStream& operator= (const IfstreamInputStream& other) = delete;
  
  IfstreamInputStream(IfstreamInputStream&& other);
  IfstreamInputStream& operator= (IfstreamInputStream&& other);
  
  /// Opens the file at the given path for reading.
  /// Returns true on success, false on failure.
  bool Open(const fs::path& path, bool isRelativeToAppPath = false);
  
  /// Closes the attached file if it is open.
  void Close();
  
  virtual usize Read(void* data, usize size) override;
  virtual bool Seek(u64 offsetFromStart) override;
  virtual u64 SizeInBytes() override;
  
 private:
  ifstream file;
};

#ifdef __ANDROID__
/// Implementation of InputStream for Android assets.
class AAssetInputStream : public InputStream {
 public:
  inline AAssetInputStream() {};
  
  AAssetInputStream(const AAssetInputStream& other) = delete;
  AAssetInputStream& operator= (const AAssetInputStream& other) = delete;
  
  AAssetInputStream(AAssetInputStream&& other);
  AAssetInputStream& operator= (AAssetInputStream&& other);
  
  ~AAssetInputStream();
  
  /// Opens the asset at the given path for reading.
  /// Returns true on success, false on failure.
  bool Open(const fs::path& path, bool isRelativeToAppPath, AAssetManager* assetManager);
  
  /// Closes the attached asset if it is open.
  void Close();
  
  virtual usize Read(void* data, usize size) override;
  virtual bool Seek(u64 offsetFromStart) override;
  virtual u64 SizeInBytes() override;
  
 private:
  AAssetManager* assetManager = nullptr;
  AAsset* asset = nullptr;
};
#endif

/// Static factory function that returns an AAssetInputStream or a IfstreamInputStream depending on whether the platform is Android or not.
/// For Android, uses the global asset manager pointer gAssetManager.
/// The returned object must be deleted with delete.
/// If the asset file cannot be opened, nullptr is returned.
InputStream* OpenAsset(const fs::path& path, bool isRelativeToAppPath);
unique_ptr<InputStream> OpenAssetUnique(const fs::path& path, bool isRelativeToAppPath);

/// Input stream for reading from a fixed-size memory region.
class MemoryInputStream : public InputStream {
 public:
  inline MemoryInputStream() {}
  
  MemoryInputStream(const MemoryInputStream& other) = delete;
  MemoryInputStream& operator= (const MemoryInputStream& other) = delete;
  
  MemoryInputStream(MemoryInputStream&& other);
  MemoryInputStream& operator= (MemoryInputStream&& other);
  
  void SetSource(const void* source, usize size);
  
  virtual usize Read(void* data, usize size) override;
  virtual bool Seek(u64 offsetFromStart) override;
  virtual u64 SizeInBytes() override;
  
 private:
  const void* baseAddress = nullptr;
  const void* readPtr = nullptr;
  usize size;
};

/// Input stream for reading from a fixed-size memory region in form of a std::vector that is owned by the input stream object.
class VectorInputStream : public InputStream {
 public:
  inline VectorInputStream() {}
  VectorInputStream(vector<u8>&& source);
  
  VectorInputStream(const MemoryInputStream& other) = delete;
  VectorInputStream& operator= (const MemoryInputStream& other) = delete;
  
  VectorInputStream(VectorInputStream&& other);
  VectorInputStream& operator= (VectorInputStream&& other);
  
  void SetSource(vector<u8>&& source);
  
  virtual usize Read(void* data, usize size) override;
  virtual bool Seek(u64 offsetFromStart) override;
  virtual u64 SizeInBytes() override;
  
 private:
  vector<u8> data;
  const void* readPtr = nullptr;
};

}
