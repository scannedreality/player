#include "scan_studio/viewer_common/xrvideo/index.hpp"

#include <limits>
#include <memory>

#include <zstd.h>

#include <loguru.hpp>

#include "scan_studio/common/xrvideo_file.hpp"

namespace scan_studio {

FrameIndex::FrameIndex() {}

bool FrameIndex::CreateFromIndexChunk(XRVideoReader* reader) {
  Clear();
  
  // Skip over the chunk header
  if (!reader->Seek(reader->GetFileOffset() + XRVideoChunkHeaderScheme::GetConstantSize())) {
    LOG(ERROR) << "Failed to read index from chunk: Unexpected EOF while seeking over the chunk header";
    return false;
  }
  
  // Read the chunk scheme
  vector<u8> buffer(XRVideoIndexChunkScheme::GetConstantSize());
  if (reader->Read(buffer.size(), buffer.data()) != buffer.size()) {
    LOG(ERROR) << "Failed to read index from chunk: Failed to read the chunk scheme data";
    return false;
  }
  
  // Parse the chunk scheme
  u8 version;
  u32 compressedIndexArraySize;
  auto schemeReader = StructuredVectorReader<XRVideoIndexChunkScheme>(buffer)
      .Read(&version);
  if (version != xrVideoIndexChunkSchemeCurrentVersion) {
    LOG(WARNING) << "Encountered an index chunk with an unknown version: " << static_cast<int>(version);
    return false;
  }
  
  schemeReader.Read(&compressedIndexArraySize);
  
  // Read the compressed index array
  vector<u8> compressedIndexArray(compressedIndexArraySize);
  if (reader->Read(compressedIndexArray.size(), compressedIndexArray.data()) != compressedIndexArray.size()) {
    LOG(ERROR) << "Failed to read index from chunk: Failed to read the compressed index array data";
    return false;
  }
  
  // Decompress the index array
  shared_ptr<ZSTD_DCtx> zstdCtx(ZSTD_createDCtx(), [](ZSTD_DCtx* ctx) { ZSTD_freeDCtx(ctx); });
  
  const unsigned long long indexArraySize = ZSTD_getFrameContentSize(compressedIndexArray.data(), compressedIndexArraySize);
  if (indexArraySize == ZSTD_CONTENTSIZE_UNKNOWN || indexArraySize == ZSTD_CONTENTSIZE_ERROR) {
    LOG(ERROR) << "ZSTD_getFrameContentSize() failed, return value: " << indexArraySize << " (compressedIndexArraySize: " << compressedIndexArraySize << ")";
    return false;
  }
  
  vector<u8> indexArray(indexArraySize);
  const usize decompressedBytes = ZSTD_decompressDCtx(zstdCtx.get(), indexArray.data(), indexArraySize, compressedIndexArray.data(), compressedIndexArraySize);
  if (ZSTD_isError(decompressedBytes)) {
    LOG(ERROR) << "Error decompressing index chunk data with zstd: " << ZSTD_getErrorName(decompressedBytes)
               << " (compressedIndexArraySize: " << compressedIndexArraySize << ", indexArraySize: " << indexArraySize << ")";
    return false;
  }
  
  // Seek to the first frame chunk to determine the file offset on which to start summing the frame sizes onto
  if (!reader->FindNextChunk(xrVideoFrameChunkIdentifierV0)) {
    LOG(ERROR) << "Failed to read index from chunk: Failed to seek to the first frame chunk";
    return false;
  }
  
  // Parse the index array
  const usize indexArrayItemSize = XRVideoIndexArrayItemScheme::GetConstantSize();
  const usize frameCount = (indexArraySize - sizeof(s64)) / indexArrayItemSize;
  frames.reserve(frameCount);
  
  const u32 chunkHeaderSize = XRVideoChunkHeaderScheme::GetConstantSize();
  
  u64 currentFileOffset = reader->GetFileOffset();
  
  for (usize frameIndex = 0; frameIndex < frameCount; ++ frameIndex) {
    u32 frameSizeInBytesAndIsKeyframeFlag;
    s64 frameStartTimestamp;
    StructuredVectorReader<XRVideoIndexArrayItemScheme>(indexArray, frameIndex * indexArrayItemSize)
        .Read(&frameSizeInBytesAndIsKeyframeFlag)
        .Read(&frameStartTimestamp);
    
    PushFrame(frameStartTimestamp, currentFileOffset, frameSizeInBytesAndIsKeyframeFlag & xrVideoIndexArrayItemIsKeyframeBit);
    currentFileOffset += (chunkHeaderSize + (frameSizeInBytesAndIsKeyframeFlag & ~xrVideoIndexArrayItemIsKeyframeBit));
  }
  
  s64 lastFrameEndTimestamp;
  memcpy(&lastFrameEndTimestamp, indexArray.data() + indexArray.size() - sizeof(lastFrameEndTimestamp), sizeof(lastFrameEndTimestamp));
  PushVideoEnd(lastFrameEndTimestamp, currentFileOffset);
  
  return true;
}

void FrameIndex::Clear() {
  frames.clear();
}

void FrameIndex::PushFrame(s64 startTimestamp, u64 offset, bool isKeyframe) {
  frames.emplace_back(startTimestamp, offset, isKeyframe);
}

void FrameIndex::PushVideoEnd(s64 endTimestamp, u64 endOffset) {
  frames.emplace_back(endTimestamp, endOffset, /*isKeyframe*/ false);
}

int FrameIndex::FindFrameIndexForTimestamp(s64 timestamp) const {
  if (timestamp < GetVideoStartTimestamp()) { return -1; }
  if (timestamp > GetVideoEndTimestamp()) { return -1; }
  
  int lowest = 0;
  int highest = frames.size() - 2;  // exclude the dummy item at the end
  
  while (lowest < highest) {
    const int mid = (lowest + highest + 1) / 2;
    
    if (frames[mid].GetTimestamp() > timestamp) {
      highest = mid - 1;
    } else {
      lowest = mid;
    }
  }
  
  return lowest;  // == highest
}

void FrameIndex::FindDependencyFrames(int frameIndex, int* baseKeyframeIfNeeded, int* predecessorIfNeeded) const {
  *baseKeyframeIfNeeded = -1;
  *predecessorIfNeeded = -1;
  
  *baseKeyframeIfNeeded = frameIndex;
  
  while (*baseKeyframeIfNeeded >= 0 &&
         !At(*baseKeyframeIfNeeded).IsKeyframe()) {
    -- *baseKeyframeIfNeeded;
  }
  if (*baseKeyframeIfNeeded < 0) {
    // This should never happen in theory, since the first frame should
    // always be guaranteed to be a keyframe.
    LOG(ERROR) << "Did not find any keyframe preceding frame " << frameIndex;
    return;
  }
  
  if (frameIndex == *baseKeyframeIfNeeded) {
    *predecessorIfNeeded = -1;
    *baseKeyframeIfNeeded = -1;
  } else {
    *predecessorIfNeeded = frameIndex - 1;
  }
}

}

