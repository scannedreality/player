#include "libvis/io/output_stream.h"

#include <cstring>

#include "libvis/io/util.h"

namespace vis {

FileOutputStream::~FileOutputStream() {
  Close();
}

FileOutputStream::FileOutputStream(FileOutputStream&& other)
    : file(other.file) {
  other.file = nullptr;
}

FileOutputStream& FileOutputStream::operator=(FileOutputStream&& other) {
  Close();
  Attach(other.Detach());
  return *this;
}

bool FileOutputStream::Open(const fs::path& path) {
  Close();
  file = fopen(path.string().c_str(), "wb");
  return file != nullptr;
}

void FileOutputStream::Close() {
  if (file) {
    fclose(file);
    file = nullptr;
  }
}

void FileOutputStream::Attach(FILE* file) {
  Close();
  this->file = file;
}

FILE* FileOutputStream::Detach() {
  FILE* old = file;
  file = nullptr;
  return old;
}

usize FileOutputStream::Write(const void* data, usize size) {
  if (!file) {
    return 0;
  }
  return fwrite(data, 1, size, file);
}

bool FileOutputStream::Seek(usize offsetFromStart) {
  return file && portable_fseek(file, offsetFromStart, SEEK_SET) == 0;
}


FixedMemoryOutputStream::FixedMemoryOutputStream(FixedMemoryOutputStream&& other)
    : baseAddress(other.baseAddress),
      writePtr(other.writePtr),
      size(other.size) {}

FixedMemoryOutputStream& FixedMemoryOutputStream::operator=(FixedMemoryOutputStream&& other) {
  baseAddress = other.baseAddress;
  writePtr = other.writePtr;
  size = other.size;
  return *this;
}

void FixedMemoryOutputStream::SetDest(void* dest, usize size) {
  baseAddress = dest;
  writePtr = dest;
  this->size = size;
}

usize FixedMemoryOutputStream::Write(const void* data, usize size) {
  const usize writableSize = min<usize>(size, this->size - static_cast<uintptr_t>(static_cast<u8*>(writePtr) - static_cast<u8*>(baseAddress)));
  memcpy(writePtr, data, writableSize);
  writePtr = static_cast<void*>(static_cast<u8*>(writePtr) + writableSize);
  return writableSize;
}

bool FixedMemoryOutputStream::Seek(usize offsetFromStart) {
  if (offsetFromStart > size) { return false; }
  writePtr = static_cast<void*>(static_cast<u8*>(baseAddress) + offsetFromStart);
  return true;
}


ResizableVectorOutputStream::ResizableVectorOutputStream(ResizableVectorOutputStream&& other)
    : dest(other.dest),
      offset(other.offset) {}

ResizableVectorOutputStream& ResizableVectorOutputStream::operator=(ResizableVectorOutputStream&& other) {
  dest = other.dest;
  offset = other.offset;
  return *this;
}

void ResizableVectorOutputStream::SetVector(vector<u8>* dest, usize offset) {
  this->dest = dest;
  this->offset = offset;
}

usize ResizableVectorOutputStream::Write(const void* data, usize size) {
  if (offset + size > dest->size()) {
    dest->resize(offset + size);
  }
  memcpy(dest->data() + offset, data, size);
  offset += size;
  return size;
}

bool ResizableVectorOutputStream::Seek(usize offsetFromStart) {
  // Notice that we allow seeking past the end of the vector.
  // The next call to Write() will resize the vector accordingly.
  // Any parts of the vector not written to will be in the state left by std::vector::resize()
  // (TODO: not sure right now whether that leaves them uninitialized or default-initialized).
  offset = offsetFromStart;
  return true;
}

}
