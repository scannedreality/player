#pragma once

#include <libvis/io/filesystem.h>
#include <vector>

#include "libvis/vulkan/libvis.h"

namespace vis {
using namespace std;

/// Virtual base class for output streams.
///
/// By using this class for data output, one can generically support
/// writing data to different destinations such as files, memory, or sockets,
/// instead of hard-coding a specific type, or re-implementing output to each of them.
///
/// The class implementing the generic output should simply take a pointer to a
/// OutputStream instance and call its Write() (and optionally, Seek())
/// functions. The user of the class should be responsible for creating the
/// output stream (and if neccessary, opening the file, for example).
class OutputStream {
 public:
  virtual inline ~OutputStream() {}
  
  /// Writes the given data to the output.
  /// Returns the number of successfully written bytes.
  virtual usize Write(const void* data, usize size) = 0;
  
  /// Helper that calls Write() and returns true only if all of the
  /// requested number of bytes were written successfully.
  inline bool WriteFully(const void* data, usize size) {
    return Write(data, size) == size;
  }
  
  /// Seeks to the given position in the output, measured as offset from the starting position in bytes.
  /// Note that sockets do not support this, so this must be avoided if operating on sockets.
  /// Returns true on success, false on failure.
  virtual bool Seek(usize offsetFromStart) = 0;
};

/// Output stream for writing to a file, using a FILE pointer.
/// TODO: Could add an option to toggle to use unbuffered writes, using a Unix file descriptor.
class FileOutputStream : public OutputStream {
 public:
  inline FileOutputStream() {}
  
  /// Destructor, closes the attached file unless Detach() has been called.
  ~FileOutputStream();
  
  FileOutputStream(const FileOutputStream& other) = delete;
  FileOutputStream& operator= (const FileOutputStream& other) = delete;
  
  FileOutputStream(FileOutputStream&& other);
  FileOutputStream& operator= (FileOutputStream&& other);
  
  /// Opens the file at the given path for writing.
  /// Returns true on success, false on failure.
  bool Open(const fs::path& path);
  
  /// Closes the attached file if it is open.
  void Close();
  
  /// Attaches to the given already-open FILE.
  void Attach(FILE* file);
  
  /// Detaches from the underlying FILE pointer, returning the detached pointer.
  /// After calling this, destructing the FileOutputStream will not close the FILE,
  /// instead the caller is responsible for closing it.
  FILE* Detach();
  
  virtual usize Write(const void* data, usize size) override;
  virtual bool Seek(usize offsetFromStart) override;
  
  inline FILE* GetFile() const { return file; }
  
 private:
  FILE* file = nullptr;
};

/// Output stream for writing to a fixed-size region of memory.
/// Any attempts to write beyond the end of this region will fail.
class FixedMemoryOutputStream : public OutputStream {
 public:
  inline FixedMemoryOutputStream() {}
  
  FixedMemoryOutputStream(const FixedMemoryOutputStream& other) = delete;
  FixedMemoryOutputStream& operator= (const FixedMemoryOutputStream& other) = delete;
  
  FixedMemoryOutputStream(FixedMemoryOutputStream&& other);
  FixedMemoryOutputStream& operator= (FixedMemoryOutputStream&& other);
  
  void SetDest(void* dest, usize size);
  
  virtual usize Write(const void* data, usize size) override;
  virtual bool Seek(usize offsetFromStart) override;
  
 private:
  void* baseAddress = nullptr;
  void* writePtr = nullptr;
  usize size;
};

/// Output stream for writing to a std::vector,
/// automatically growing the vector if needed.
///
/// Keeps a raw pointer to the vector, which thus needs to remain valid
/// while the ResizableVectorOutputStream is used.
class ResizableVectorOutputStream : public OutputStream {
 public:
  inline ResizableVectorOutputStream() {}
  
  ResizableVectorOutputStream(const ResizableVectorOutputStream& other) = delete;
  ResizableVectorOutputStream& operator= (const ResizableVectorOutputStream& other) = delete;
  
  ResizableVectorOutputStream(ResizableVectorOutputStream&& other);
  ResizableVectorOutputStream& operator= (ResizableVectorOutputStream&& other);
  
  void SetVector(vector<u8>* dest, usize offset = 0);
  
  virtual usize Write(const void* data, usize size) override;
  virtual bool Seek(usize offsetFromStart) override;
  
 private:
  vector<u8>* dest = nullptr;
  usize offset = 0;
};

// TODO: Possible additions:
//       - OfstreamOutputStream
//       - SocketOutputStream

}
