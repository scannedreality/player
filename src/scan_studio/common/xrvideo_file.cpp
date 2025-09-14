#include "scan_studio/common/xrvideo_file.hpp"

#include <loguru.hpp>

#include <libvis/io/input_stream.h>

namespace scan_studio {

XRVideoReader::~XRVideoReader() {
  Close();
}

XRVideoReader::XRVideoReader(XRVideoReader&& other)
    : inputStream(other.inputStream),
      peekBuffer(std::move(other.peekBuffer)),
      currentFileOffset(other.currentFileOffset) {
  other.inputStream = nullptr;
}

XRVideoReader& XRVideoReader::operator=(XRVideoReader&& other) {
  swap(inputStream, other.inputStream);
  swap(peekBuffer, other.peekBuffer);
  swap(currentFileOffset, other.currentFileOffset);
  
  return *this;
}

void XRVideoReader::TakeInputStream(InputStream* inputStream, bool isStreamingInputStream) {
  Close();
  peekBuffer.clear();
  currentFileOffset = 0;
  this->inputStream = inputStream;
  usingStreamingInputStream = isStreamingInputStream;
}

void XRVideoReader::Close() {
  if (inputStream) {
    delete inputStream;
    inputStream = nullptr;
  }
}

bool XRVideoReader::ReadMetadata(XRVideoMetadata* metadata) {
  if (!FindNextChunk(xrVideoMetadataChunkIdentifierV0)) { return false; }
  if (!Seek(currentFileOffset + XRVideoChunkHeaderScheme::GetConstantSize())) { return false; }
  
  vector<u8> buffer(XRVideoMetadataChunkScheme::GetConstantSize());
  if (Read(buffer.size(), buffer.data()) != buffer.size()) { return false; }
  
  u8 version;
  auto reader = StructuredVectorReader<XRVideoMetadataChunkScheme>(buffer)
      .Read(&version);
  if (version != xrVideoMetadataChunkSchemeCurrentVersion) {
    LOG(WARNING) << "Encountered a metadata chunk with an unknown version: " << static_cast<int>(version);
    return false;
  }
  
  reader
      .Read(&metadata->lookAtX)
      .Read(&metadata->lookAtY)
      .Read(&metadata->lookAtZ)
      .Read(&metadata->radius)
      .Read(&metadata->yaw)
      .Read(&metadata->pitch);
  return true;
}

bool XRVideoReader::FindNextChunk(u8 chunkIdentifier) {
  // For header chunks, start searching at the file start
  const bool searchingForHeaderChunk = IsXRVideoHeaderChunk(chunkIdentifier);
  if (searchingForHeaderChunk && !Seek(0)) { return false; }
  
  u32 chunkSizeWithoutHeader;
  u8 chunkType;
  
  while (true) {
    // Peek for the header of the next chunk and parse it
    if (!ParseChunkHeader(&chunkSizeWithoutHeader, &chunkType)) { return false; }
    
    if (chunkType == chunkIdentifier) {
      // Found the chunk type we were looking for
      return true;
    } else if (searchingForHeaderChunk && IsXRVideoFrameChunk(chunkType)) {
      // No header chunk may follow a frame chunk.
      // Thus, no header chunk with the given identifier exists,
      // and we can thus stop our search early (instead of seeking over the whole rest of the file).
      return false;
    } else {
      // Skip over the chunk.
      if (!Seek(currentFileOffset + XRVideoChunkHeaderScheme::GetConstantSize() + chunkSizeWithoutHeader)) { return false; }
    }
  }
}

bool XRVideoReader::ParseChunkHeader(u32* chunkSizeWithoutHeader, u8* chunkType) {
  if (!Peek(XRVideoChunkHeaderScheme::GetConstantSize())) { return false; }
  StructuredPtrReader<XRVideoChunkHeaderScheme>(peekBuffer.data())
      .Read(chunkSizeWithoutHeader)
      .Read(chunkType);
  return true;
}

bool XRVideoReader::ReadNextFrame(vector<u8>* data, u64* fileOffset) {
  // Seek to the next frame chunk and output its file offset if desired
  if (!FindNextChunk(xrVideoFrameChunkIdentifierV0)) { return false; }
  if (fileOffset) {
    *fileOffset = currentFileOffset;
  }
  
  u32 chunkSizeWithoutHeader;
  u8 chunkType;
  if (!ParseChunkHeader(&chunkSizeWithoutHeader, &chunkType)) { return false; }
  if (!Seek(currentFileOffset + XRVideoChunkHeaderScheme::GetConstantSize())) { return false; }
  
  // Read the frame data
  data->resize(chunkSizeWithoutHeader);
  if (Read(chunkSizeWithoutHeader, data->data()) != chunkSizeWithoutHeader) {
    if (!aborted) { LOG(WARNING) << "File is truncated"; }
    return false;
  }
  
  return true;
}

bool XRVideoReader::Seek(u64 fileOffset) {
  if (fileOffset == currentFileOffset) {
    return true;
  }
  
  if (!inputStream->Seek(fileOffset)) {
    return false;
  }
  
  currentFileOffset = fileOffset;
  peekBuffer.clear();
  return true;
}

usize XRVideoReader::Read(usize bytes, u8* dest) {
  if (bytes <= peekBuffer.size()) {
    // Take all bytes from the peekBuffer
    memcpy(dest, peekBuffer.data(), bytes);
    peekBuffer.erase(peekBuffer.begin() + 0, peekBuffer.begin() + bytes);
    currentFileOffset += bytes;
    return bytes;
  }
  
  // Copy the whole peekBuffer into dest and read from the inputStream in addition to that
  memcpy(dest, peekBuffer.data(), peekBuffer.size());
  const usize missingByteCount = bytes - peekBuffer.size();
  
  const usize bytesRead = peekBuffer.size() + inputStream->Read(dest + peekBuffer.size(), missingByteCount);
  peekBuffer.clear();
  
  currentFileOffset += bytesRead;
  return bytesRead;
}

void XRVideoReader::AbortRead() {
  aborted = true;
  inputStream->AbortRead();
}

bool XRVideoReader::Peek(usize bytes) {
  if (peekBuffer.size() >= bytes) {
    return true;
  }
  
  const usize missingByteCount = bytes - peekBuffer.size();
  peekBuffer.resize(bytes);
  
  const usize bytesRead = inputStream->Read(peekBuffer.data() + peekBuffer.size() - missingByteCount, missingByteCount);
  if (bytesRead == missingByteCount) {
    return true;
  }
  
  // Failed to read all missing bytes, resize peekBuffer to its actual content
  peekBuffer.resize(peekBuffer.size() - missingByteCount + bytesRead);
  return false;
}

}
