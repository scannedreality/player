#pragma once

#include <fstream>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/io/structured_io.hpp"

namespace vis {
class InputStream;
}

namespace scan_studio {
using namespace vis;

/// XRVideo files consist of a number of chunks.
///
/// Each chunk begins with XRVideoChunkHeaderScheme.
/// This allows applications to skip over any chunks that they don't recognize
/// (or that they don't want to spend the effort on to parse them).
///
/// Chunks can be classified as header chunks or frame chunks.
/// Header chunks may only appear at the start of the file, before any frame chunk.
/// Frame chunks may only appear after all header chunks (if any).
typedef BufferScheme<
    BufferField<u32>,  // chunk size in bytes, excluding the size of this chunk header (XRVideoChunkHeaderScheme::GetConstantSize()).
    BufferField<u8>    // chunk type
    > XRVideoChunkHeaderScheme;

// List of XRVideo chunk types
constexpr u8 xrVideoFrameChunkIdentifierV0 = 0;     // an XRVideo frame              -   frame chunk  -  version 0
constexpr u8 xrVideoMetadataChunkIdentifierV0 = 1;  // XRVideo file metadata         -  header chunk  -  version 0
constexpr u8 xrVideoIndexChunkIdentifierV0 = 2;     // an index of the XRVideo file  -  header chunk  -  version 0

/// Returns whether we know that the given chunk type is a header chunk.
/// Attention: For a given chunkIdentifier, the result of this function is not necessarily the inverse of IsXRVideoFrameChunk(chunkIdentifier)!
///            This is because the chunk identifier may be unknown to us. In that case, neither of the functions will return true.
///            Thus, consider carefully which property of a chunk you want to assume by default for unknown chunks when calling IsXRVideoHeaderChunk() and / or IsXRVideoFrameChunk().
inline bool IsXRVideoHeaderChunk(u8 chunkIdentifier) {
  return chunkIdentifier == xrVideoMetadataChunkIdentifierV0 ||
         chunkIdentifier == xrVideoIndexChunkIdentifierV0;
}

/// Returns whether we know that the given chunk type is a frame chunk.
/// Attention: See the comment on IsXRVideoHeaderChunk() for behavior for unknown chunks.
inline bool IsXRVideoFrameChunk(u8 chunkIdentifier) {
  return chunkIdentifier == xrVideoFrameChunkIdentifierV0;
}


// --- XRVideo frame chunk (xrVideoFrameChunkIdentifierV0) ---
/// This is followed by XRVideoKeyframeHeaderScheme for keyframes, or directly by the frame data for follow-up frames (non-keyframes).
typedef BufferScheme<
    BufferField<u8>,      // version (frame chunks have a separate version such that we can keep their chunk header type if we increase the version;
                          //          this way, old applications can still identify them as frame chunks; set to xrVideoHeaderSchemeCurrentVersion)
    BufferField<u8>,      // bitflags (see below for possible values; the other bits are still unused and always set to zero)
    BufferField<u16>,     // deformation node count (stored here even for non-keyframes in order to enable independent loading of the frames in advance)
    BufferField<s64>,     // start timestamp (in nanoseconds)
    BufferField<s64>,     // end timestamp (in nanoseconds), should be equal to the next frame's start timestamp
    BufferField<u32>,     // texture width
    BufferField<u32>,     // texture height
    BufferField<u32>,     // size of compressed deformation state data
    BufferField<u32>      // size of compressed yuv data (respectively compressed rgb data, if XRVideoZStdRGBTextureBitflag is set in `bitflags`)
    > XRVideoHeaderScheme;

constexpr u8 xrVideoHeaderSchemeCurrentVersion = 0;

constexpr u32 XRVideoHeaderScheme_compressedDeformationStateSize_offset = 28;
typedef u32 XRVideoHeaderScheme_compressedDeformationStateSize_type;

/// Bitflag values for the bitflags attribute
constexpr u8 XRVideoIsKeyframeBitflag = (1 << 0);
constexpr u8 XRVideoHasVertexAlphaBitflag = (1 << 1);
constexpr u8 XRVideoZStdRGBTextureBitflag = (1 << 2);

/// Follows XRVideoHeaderScheme for keyframes.
typedef BufferScheme<
    BufferField<u16>,       // unique vertex count
    BufferField<u16>,       // vertex count
    BufferField<u32>,       // triangle count
    BufferArray<6, float>,  // vertices bounding box (min coordinates and conversion factors. Not (directly) the max coordinates!)
    BufferField<u32>,       // size of compressed mesh data
    BufferField<u32>        // size of decompressed but still encoded deformation graph data
    > XRVideoKeyframeHeaderScheme;

// After the header(s), these buffers follow:
//
// - If the frame is a keyframe:
//   - Compressed mesh, consisting of:
//     * Vertices
//     * Indices
//     * Texture coordinates
//     * Deformation graph
// - Compressed deformation state (which aligns the current frame with the following frame)
// - Compressed texture yuv (with AV.1) or texture rgb (with zstd)
// - Compressed vertex alpha values (if XRVideoHasVertexAlphaBitflag is set)
//
// These are not included in the message schemes above.


// --- XRVideo file metadata chunk (xrVideoMetadataChunkIdentifierV0) ---
/// This defines the metadata chunk.
/// Zero or one metadata chunk may be present among the XRVideo's header chunks.
/// No metadata chunks are allowed afterwards.
typedef BufferScheme<
    BufferField<u8>,      // version (set to XRVideoMetadataChunkSchemeCurrentVersion)
    BufferField<float>,   // initial view: lookAtX
    BufferField<float>,   // initial view: lookAtY
    BufferField<float>,   // initial view: lookAtZ
    BufferField<float>,   // initial view: radius
    BufferField<float>,   // initial view: yaw
    BufferField<float>    // initial view: pitch
    > XRVideoMetadataChunkScheme;

constexpr u8 xrVideoMetadataChunkSchemeCurrentVersion = 0;

struct XRVideoMetadata {
  u8 version = xrVideoMetadataChunkSchemeCurrentVersion;
  float lookAtX;
  float lookAtY;
  float lookAtZ;
  float radius;
  float yaw;
  float pitch;
  
  inline vector<u8> SerializeToChunk() const {
    vector<u8> result(XRVideoChunkHeaderScheme::GetConstantSize() + XRVideoMetadataChunkScheme::GetConstantSize());
    StructuredVectorWriter<XRVideoChunkHeaderScheme>(&result)
        .Write(XRVideoMetadataChunkScheme::GetConstantSize())
        .Write(xrVideoMetadataChunkIdentifierV0);
    StructuredVectorWriter<XRVideoMetadataChunkScheme>(&result, XRVideoChunkHeaderScheme::GetConstantSize())
        .Write(version)
        .Write(lookAtX)
        .Write(lookAtY)
        .Write(lookAtZ)
        .Write(radius)
        .Write(yaw)
        .Write(pitch);
    return result;
  }
};


// --- XRVideo file index chunk (xrVideoIndexChunkIdentifierV0) ---
/// This defines the index chunk.
/// Zero or one index chunks may be present among the XRVideo's header chunks.
/// No index chunks are allowed afterwards.
typedef BufferScheme<
    BufferField<u8>,      // version (set to XRVideoIndexChunkSchemeCurrentVersion)
    BufferField<u32>      // size of the compressed chunk data that follows
    // This is followed by the zstd-compressed chunk data (which is not represented in this scheme).
    // Decompressing the data yields an index array that is similar, but not equal to the data stored by the FrameIndex class.
    // * The difference is that in order to improve the compressibility, the index array stores the size in bytes of each frame,
    //   rather than the starting offset of each frame in the file (that in addition also depends on the size of the compressed index chunk,
    //   and thus cannot even be determined before compressing the chunk).
    // * For each frame, the following data (following XRVideoIndexArrayItemScheme) is in the index array:
    //   - u32 frameSizeInBytesAndIsKeyframeFlag;  // with the first bit being a flag that is set to 1 for keyframes, and 0 for non-keyframes.
    //   - s64 frameStartTimestampInNanoseconds;
    //   After the data of all frames, a single s64 follows that gives the end timestamp of the last frame in the video in nanoseconds.
    > XRVideoIndexChunkScheme;

constexpr u8 xrVideoIndexChunkSchemeCurrentVersion = 0;

typedef BufferScheme<
    BufferField<u32>,  // frameSizeInBytesAndIsKeyframeFlag, with the frame size excluding the frame chunk header
    BufferField<s64>   // frameStartTimestamp in nanoseconds
    > XRVideoIndexArrayItemScheme;

constexpr static u32 xrVideoIndexArrayItemIsKeyframeBit = static_cast<u32>(1) << 31;


class FrameIndex;
class StreamingInputStream;

class XRVideoReader {
 public:
  inline XRVideoReader() {}
  ~XRVideoReader();
  
  XRVideoReader(XRVideoReader&& other);
  XRVideoReader& operator= (XRVideoReader&& other);
  
  XRVideoReader(const XRVideoReader& other) = delete;
  XRVideoReader& operator= (const XRVideoReader& other) = delete;
  
  /// Takes ownership of the given inputStream for reading the XRVideo.
  /// Will delete the inputStream with `delete` on destruction, so it must be allocated with `new`.
  ///
  /// `isStreamingInputStream` is used to avoid the need for dynamic_cast
  /// or having an RTTI mechanism built into libvis' `InputStream`.
  /// It must be set to true if a StreamingInputStream is passed in, false otherwise.
  /// This is used to improve streaming performance by calling additional functions
  /// on StreamingInputStream to pre-read data.
  void TakeInputStream(InputStream* inputStream, bool isStreamingInputStream);
  
  /// Closes and destroys the input stream.
  void Close();
  
  /// Reads the metadata header chunk, if any.
  /// Returns true if a metadata chunk was read,
  /// false if no metadata chunk exists or if there was an error.
  bool ReadMetadata(XRVideoMetadata* metadata);
  
  /// Tries to find the chunk with the given identifier (e.g., xrVideoIndexChunkIdentifierV0)
  /// in the file. If the requested chunk is a header chunk, automatically starts searching
  /// at the file start; otherwise, starts searching from the current file cursor position.
  /// Returns true if the given chunk was found. In that case, the resulting file cursor
  /// position after calling this function is at the start of that chunk('s header).
  /// Returns false if the given chunk was not found. In that case, the resulting
  /// file cursor position is undefined.
  bool FindNextChunk(u8 chunkIdentifier);
  
  /// Precondition: The current file offset is at a chunk header start.
  /// Peeks for this header and parses it, returning its values.
  /// Returns true if successful, false on failure.
  bool ParseChunkHeader(u32* chunkSizeWithoutHeader, u8* chunkType);
  
  /// Tries to read the next frame chunk in the XRVideo.
  /// Returns true on success (with the data in *data), false on failure or end-of-file.
  /// Optionally returns the frame's file offset in fileOffset.
  bool ReadNextFrame(vector<u8>* data, u64* fileOffset = nullptr);
  
  /// Seeks to the given file offset.
  /// Returns true on success, false on failure.
  bool Seek(u64 fileOffset);
  
  /// Tries to read the given number of bytes into dest, taking into account that there may be
  /// content in peekBuffer that must be used before continuing to read from the inputStream.
  /// Returns the number of bytes read (which may be smaller than requested in case of an error
  /// or end-of-file).
  usize Read(usize bytes, u8* dest);
  
  /// If a Read() call by another thread is in progress, tries to abort it.
  /// This is likely not implemented for file-based streams, but is useful for streams that
  /// receive data from the network. These may stall in case the network connection drops,
  /// requiring to abort the read.
  void AbortRead();
  
  /// Returns whether the XRVideo reader has an open input stream.
  inline bool IsOpen() const { return inputStream != nullptr; }
  
  /// Returns the current file offset of the reader.
  inline u64 GetFileOffset() const { return currentFileOffset; }
  
  /// Returns whether a StreamingInputStream is used as input.
  inline bool UsesStreamingInputStream() const { return usingStreamingInputStream; }
  
  /// If a StreamingInputStream is used for input, provides access to it to be able to implement special-case actions for streaming.
  inline StreamingInputStream* GetStreamingInputStream() const { return usingStreamingInputStream ? reinterpret_cast<StreamingInputStream*>(inputStream) : nullptr; }
  
 private:
  /// Tries to read data from the file such that there are at least the requested number of bytes
  /// in peekBuffer. Returns true if successful, false if not enough bytes could be read before the
  /// end of file or before an I/O error occurred.
  bool Peek(usize bytes);
  
  InputStream* inputStream = nullptr;
  vector<u8> peekBuffer;
  u64 currentFileOffset = 0;
  bool aborted = false;
  bool usingStreamingInputStream;
};

}
