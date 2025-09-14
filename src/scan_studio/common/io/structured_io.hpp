#pragma once

#include <cstring>
#include <vector>

#include <loguru.hpp>

#include <libvis/io/util.h>

#include "scan_studio/common/common_defines.hpp"

namespace scan_studio {
using namespace vis;

/// Structured buffer element: Field
template <typename FieldT>
class BufferField {
 public:
  constexpr static bool hasConstantSize = true;
  constexpr static usize constantSize = sizeof(FieldT);
  constexpr static usize minimumSize = sizeof(FieldT);
};

/// Structured buffer element: Array with constant, known size
template <int count, typename ArrayElementT>
class BufferArray {
 public:
  constexpr static bool hasConstantSize = true;
  constexpr static usize constantSize = count * sizeof(ArrayElementT);
  constexpr static usize minimumSize = count * sizeof(ArrayElementT);
};

/// Structured buffer element: Sized Array (array size stored as a field, followed by the array contents)
template <typename SizeT, typename ArrayElementT>
class BufferSizedArray {
 public:
  constexpr static bool hasConstantSize = false;
  constexpr static usize constantSize = 0;  // invalid
  constexpr static usize minimumSize = sizeof(SizeT);
};

/// Structured buffer element: String
template <typename SizeT>
class BufferString {
 public:
  constexpr static bool hasConstantSize = false;
  constexpr static usize constantSize = 0;  // invalid
  constexpr static usize minimumSize = sizeof(SizeT);
};

/// Structured buffer element: Repeatable block (may repeat zero or more times)
template <class Scheme>
class BufferRepeatableBlock {
 public:
  constexpr static bool hasConstantSize = false;
  constexpr static usize constantSize = 0;  // invalid
  constexpr static usize minimumSize = 0;
};


// Last BufferScheme type in recursion.
template <class... Elements>
class BufferScheme {
 public:
  constexpr static bool HasConstantSize() {
    return true;
  }
  
  constexpr static usize GetConstantSize() {
    return 0;
  }
  
  constexpr static usize GetMinimumSize() {
    return 0;
  }
  
 protected:
  constexpr static usize GetMinimumSizeNoWarning() {
    return 0;
  }
};

// Specialization of BufferScheme for recursion, peeling off the next Element from the Elements list.
template <class Element, class... Elements>
class BufferScheme<Element, Elements...> : public BufferScheme<Elements...> {
 public:
  typedef BufferScheme<Elements...> Base;
  
  constexpr static bool HasConstantSize() {
    return Element::hasConstantSize && Base::HasConstantSize();
  }
  
  constexpr static usize GetConstantSize() {
    // static_assert(HasConstantSize(), "GetConstantSize() called on a BufferScheme that does not have a constant size");
    if (!HasConstantSize()) { LOG(ERROR) << "GetConstantSize() called on a BufferScheme that does not have a constant size"; }
    
    return Element::constantSize + Base::GetConstantSize();
  }
  
  constexpr static usize GetMinimumSize() {
    // static_assert(!HasConstantSize(), "GetMinimumSize() called on a BufferScheme that has a constant size");
    if (HasConstantSize()) { LOG(FATAL) << "GetMinimumSize() called on a BufferScheme that has a constant size"; }
    
    // Since partial schemes may have a constant size even though the full scheme has a variable size,
    // we have to use GetMinimumSizeNoWarning() here in order not to cause incorrect warnings.
    return Element::minimumSize + Base::GetMinimumSizeNoWarning();
  }
  
 protected:
  constexpr static usize GetMinimumSizeNoWarning() {
    return Element::minimumSize + Base::GetMinimumSizeNoWarning();
  }
};


// vector storage for StructuredWriter
class VectorWriteStorage {
 public:
  inline VectorWriteStorage(vector<u8>* buffer)
      : buffer(buffer),
        currentByte(0) {}
  
  inline VectorWriteStorage(vector<u8>* buffer, usize currentByte)
      : buffer(buffer),
        currentByte(currentByte) {}
  
  inline VectorWriteStorage(const VectorWriteStorage& other)
      : buffer(other.buffer),
        currentByte(other.currentByte) {}
  
  inline VectorWriteStorage& operator= (const VectorWriteStorage& other) {
    buffer = other.buffer;
    currentByte = other.currentByte;
    return *this;
  }
  
  // TODO: Other constructors?
  
  inline usize GetCurrentByte() const { return currentByte; }
  
 protected:
  inline void WriteImpl(const void* src, usize size) {
    const usize requiredSize = currentByte + size;
    if (requiredSize > buffer->size()) {
      buffer->resize(requiredSize);
    }
    
    memcpy(buffer->data() + currentByte, src, size);
    currentByte += size;
  }
  
  vector<u8>* buffer;
  usize currentByte;
};


// File storage for StructuredWriter
class FileWriteStorage {
 public:
  inline FileWriteStorage(FILE* file)
      : file(file) {}
  
  inline FileWriteStorage(const FileWriteStorage& other)
      : file(other.file) {}
  
  inline FileWriteStorage& operator= (const FileWriteStorage& other) {
    file = other.file;
    return *this;
  }
  
  // TODO: Other constructors?
  
 protected:
  inline void WriteImpl(const void* src, usize size) {
    fwrite(src, 1, size, file);
  }
  
  FILE* file;
};


// Last StructuredWriter type in recursion.
template <class Storage, class... Elements>
class StructuredWriter : public Storage {
 public:
  using Storage::Storage;
  inline StructuredWriter(const Storage& storage) : Storage(storage) {}
};

// Specialization of StructuredWriter for the current element being a BufferField. Peels off the next Element from the Elements list.
template <class Storage, typename FieldT, class... Elements>
class StructuredWriter<Storage, BufferField<FieldT>, Elements...> : protected StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
  
  Base Write(FieldT value) {
    this->WriteImpl(&value, sizeof(FieldT));
    
    return Base(*this);
  }
};

// Specialization of StructuredWriter for the current element being a BufferArray. Peels off the next Element from the Elements list.
template <class Storage, int count, typename ArrayElementT, class... Elements>
class StructuredWriter<Storage, BufferArray<count, ArrayElementT>, Elements...> : protected StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
  
  Base Write(const ArrayElementT* data) {
    this->WriteImpl(data, count * sizeof(ArrayElementT));
    
    return Base(*this);
  }
};

// Specialization of StructuredWriter for the current element being a BufferSizedArray. Peels off the next Element from the Elements list.
template <class Storage, typename SizeT, typename ArrayElementT, class... Elements>
class StructuredWriter<Storage, BufferSizedArray<SizeT, ArrayElementT>, Elements...> : protected StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
  
  Base Write(SizeT size, const ArrayElementT* data) {
    this->WriteImpl(&size, sizeof(SizeT));
    this->WriteImpl(data, size * sizeof(ArrayElementT));
    
    return Base(*this);
  }
};

// Specialization of StructuredWriter for the current element being a BufferString. Peels off the next Element from the Elements list.
template <class Storage, typename SizeT, class... Elements>
class StructuredWriter<Storage, BufferString<SizeT>, Elements...> : protected StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
  
  Base Write(SizeT size, const char* data) {
    this->WriteImpl(&size, sizeof(SizeT));
    this->WriteImpl(data, size * sizeof(char));
    
    return Base(*this);
  }
  
  Base Write(const string& data) {
    SizeT size = data.size();
    
    this->WriteImpl(&size, sizeof(SizeT));
    this->WriteImpl(data.data(), size * sizeof(char));
    
    return Base(*this);
  }
};

// Specialization of StructuredWriter for the current element being a BufferRepeatableBlock. Peels off the next Element from the Elements list.
template <class Storage, typename RepeatableScheme, class... Elements>
class StructuredWriter<Storage, BufferRepeatableBlock<RepeatableScheme>, Elements...> : public StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
  
  typedef StructuredWriter<Storage, RepeatableScheme> BlockWriter;
  
  BlockWriter StartBlock() {
    return BlockWriter(*this);
  }
  
  template <typename BlockEndWriterT>
  void RepeatableBlock(const BlockEndWriterT& blockEndWriter) {
    // Update the storage's write position
    *static_cast<Storage*>(this) = blockEndWriter;
  }
};

// Specialization of StructuredWriter for BufferSchemes, extracting the elements out of the BufferScheme,
// and holding the currentByte value.
template <class Storage, class... Elements>
class StructuredWriter<Storage, BufferScheme<Elements...>> : public StructuredWriter<Storage, Elements...> {
 public:
  typedef StructuredWriter<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredWriter(const Storage& storage) : Base(storage) {}
};

template <class... Elements>
using StructuredVectorWriter = StructuredWriter<VectorWriteStorage, Elements...>;

template <class... Elements>
using StructuredFileWriter = StructuredWriter<FileWriteStorage, Elements...>;


// vector storage for StructuredReader
class VectorReadStorage {
 public:
  inline VectorReadStorage(const vector<u8>& buffer)
      : buffer(&buffer),
        currentByte(0) {}
  
  inline VectorReadStorage(const vector<u8>& buffer, usize currentByte)
      : buffer(&buffer),
        currentByte(currentByte) {}
  
  inline VectorReadStorage(const VectorReadStorage& other)
      : buffer(other.buffer),
        currentByte(other.currentByte) {}
  
  inline VectorReadStorage& operator= (const VectorReadStorage& other) {
    buffer = other.buffer;
    currentByte = other.currentByte;
    return *this;
  }
  
  // TODO: Other constructors?
  
  inline usize GetCurrentByte() const { return currentByte; }
  
 protected:
  inline void ReadImpl(void* dest, usize size) {
    // TODO: Prevent reading beyond the buffer end for variable-sized schemes
    memcpy(dest, buffer->data() + currentByte, size);
    currentByte += size;
  }
  
  inline void PeekImpl(void* dest, usize size) {
    // TODO: Prevent reading beyond the buffer end for variable-sized schemes
    memcpy(dest, buffer->data() + currentByte, size);
  }
  
  const vector<u8>* buffer;
  usize currentByte;
};


// Pointer storage for StructuredReader
class PointerReadStorage {
 public:
  inline PointerReadStorage(const u8* ptr)
      : ptr(ptr) {}
  
  inline PointerReadStorage(const PointerReadStorage& other)
      : ptr(other.ptr) {}
  
  inline PointerReadStorage& operator= (const PointerReadStorage& other) {
    ptr = other.ptr;
    return *this;
  }
  
  // TODO: Other constructors?
  
 protected:
  inline void ReadImpl(void* dest, usize size) {
    // TODO: Prevent reading beyond the ptr end for variable-sized schemes
    memcpy(dest, ptr, size);
    ptr += size;
  }
  
  inline void PeekImpl(void* dest, usize size) {
    // TODO: Prevent reading beyond the ptr end for variable-sized schemes
    memcpy(dest, ptr, size);
  }
  
  const u8* ptr;
};


// File storage for StructuredReader
class FileReadStorage {
 public:
  inline FileReadStorage(FILE* file)
      : file(file) {}
  
  inline FileReadStorage(const FileReadStorage& other)
      : file(other.file) {}
  
  inline FileReadStorage& operator= (const FileReadStorage& other) {
    file = other.file;
    return *this;
  }
  
  // TODO: Other constructors?
  
 protected:
  inline void ReadImpl(void* dest, usize size) {
    if (fread(dest, 1, size, file) != size) {
      LOG(ERROR) << "Failed to read all " << size << " bytes";
      // TODO: Report the error to the caller
    }
  }
  
  inline void PeekImpl(void* dest, usize size) {
    if (fread(dest, 1, size, file) != size) {
      LOG(ERROR) << "Failed to read all " << size << " bytes";
      // TODO: Report the error to the caller
    }
    portable_fseek(file, -1 * static_cast<s64>(size), SEEK_CUR);
  }
  
  FILE* file;
};


// Last StructuredReader type in recursion.
template <class Storage, class... Elements>
class StructuredReader : public Storage {
 public:
  using Storage::Storage;
  inline StructuredReader(const Storage& storage) : Storage(storage) {}
};

// Specialization of StructuredReader for the current element being a BufferField. Peels off the next Element from the Elements list.
template <class Storage, typename FieldT, class... Elements>
class StructuredReader<Storage, BufferField<FieldT>, Elements...> : protected StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
  
  Base Read(FieldT* value) {
    this->ReadImpl(value, sizeof(FieldT));
    
    return Base(*this);
  }
};

// Specialization of StructuredReader for the current element being a BufferArray. Peels off the next Element from the Elements list.
template <class Storage, int count, typename ArrayElementT, class... Elements>
class StructuredReader<Storage, BufferArray<count, ArrayElementT>, Elements...> : protected StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
  
  Base Read(ArrayElementT* data) {
    this->ReadImpl(data, count * sizeof(ArrayElementT));
    
    return Base(*this);
  }
};

// Specialization of StructuredReader for the current element being a BufferSizedArray. Peels off the next Element from the Elements list.
template <class Storage, typename SizeT, typename ArrayElementT, class... Elements>
class StructuredReader<Storage, BufferSizedArray<SizeT, ArrayElementT>, Elements...> : protected StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, BufferSizedArray<SizeT, ArrayElementT>, Elements...> This;
  typedef StructuredReader<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
  
  This ReadSize(SizeT* size) {
    this->PeekImpl(size, sizeof(SizeT));
    return *this;
  }
  
  Base ReadArray(ArrayElementT* data) {
    SizeT size;
    this->ReadImpl(&size, sizeof(SizeT));
    
    this->ReadImpl(data, size * sizeof(ArrayElementT));
    
    return Base(*this);
  }
};

// Specialization of StructuredReader for the current element being a BufferString. Peels off the next Element from the Elements list.
template <class Storage, typename SizeT, class... Elements>
class StructuredReader<Storage, BufferString<SizeT>, Elements...> : protected StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, BufferString<SizeT>, Elements...> This;
  typedef StructuredReader<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
  
  This ReadSize(SizeT* size) {
    this->PeekImpl(size, sizeof(SizeT));
    return *this;
  }
  
  Base ReadString(char* data) {
    SizeT size;
    this->ReadImpl(&size, sizeof(SizeT));
    
    this->ReadImpl(data, size * sizeof(char));
    
    return Base(*this);
  }
  
  Base Read(string* data) {
    SizeT size;
    this->ReadImpl(&size, sizeof(SizeT));
    
    data->resize(size);
    this->ReadImpl(data->data(), size);
    
    return Base(*this);
  }
};

// Specialization of StructuredReader for the current element being a BufferRepeatableBlock. Peels off the next Element from the Elements list.
template <class Storage, typename RepeatableScheme, class... Elements>
class StructuredReader<Storage, BufferRepeatableBlock<RepeatableScheme>, Elements...> : public StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, Elements...> Base;
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
  
  typedef StructuredReader<Storage, RepeatableScheme> BlockReader;
  
  BlockReader StartBlock() {
    return BlockReader(*this);
  }
  
  template <typename BlockEndReaderT>
  void RepeatableBlock(const BlockEndReaderT& blockEndReader) {
    // Update the storage's read position
    *static_cast<Storage*>(this) = blockEndReader;
  }
};

// Specialization of StructuredReader for BufferSchemes, extracting the elements out of the BufferScheme
template <class Storage, class... Elements>
class StructuredReader<Storage, BufferScheme<Elements...>> : public StructuredReader<Storage, Elements...> {
 public:
  typedef StructuredReader<Storage, Elements...> Base;
  typedef BufferScheme<Elements...> Scheme;
  
  using Base::Base;
  inline StructuredReader(const Storage& storage) : Base(storage) {}
};


template <class... Elements>
class StructuredVectorReader {};

template <class... Elements>
class StructuredVectorReader<BufferScheme<Elements...>> : public StructuredReader<VectorReadStorage, Elements...> {
 public:
  typedef StructuredReader<VectorReadStorage, Elements...> Base;
  typedef BufferScheme<Elements...> Scheme;
  
  inline StructuredVectorReader(const vector<u8>& buffer)
      : Base(buffer) {
    if (Scheme::HasConstantSize() && buffer.size() < Scheme::GetConstantSize()) {
      LOG(FATAL) << "Initializing a StructuredVectorReader that uses a constant-sized scheme (size: " << Scheme::GetConstantSize() << ") on a buffer that is too small (size: " << buffer.size() << ")";
    } else if (!Scheme::HasConstantSize() && buffer.size() < Scheme::GetMinimumSize()) {
      LOG(FATAL) << "Initializing a StructuredVectorReader that uses a scheme with minimum size " << Scheme::GetMinimumSize() << " on a buffer that is too small (size: " << buffer.size() << ")";
    }
  }
  
  inline StructuredVectorReader(const vector<u8>& buffer, usize currentByte)
      : Base(buffer, currentByte) {
    if (Scheme::HasConstantSize() && buffer.size() - currentByte < Scheme::GetConstantSize()) {
      LOG(FATAL) << "Initializing a StructuredVectorReader that uses a constant-sized scheme (size: " << Scheme::GetConstantSize() << ") on a part of a buffer that is too small (size: " << (buffer.size() - currentByte) << ")";
    } else if (!Scheme::HasConstantSize() && buffer.size() - currentByte < Scheme::GetMinimumSize()) {
      LOG(FATAL) << "Initializing a StructuredVectorReader that uses a scheme with minimum size " << Scheme::GetMinimumSize() << " on a part of a buffer that is too small (size: " << (buffer.size() - currentByte) << ")";
    }
  }
};

template <class... Elements>
using StructuredPtrReader = StructuredReader<PointerReadStorage, Elements...>;

template <class... Elements>
using StructuredFileReader = StructuredReader<FileReadStorage, Elements...>;


// TODO: StructuredMessageWriter/Reader? -- using direct send() / recv() instead of writing to a vector?

}
