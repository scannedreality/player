#pragma once

#include <vector>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

class XRVideoReader;

class FrameIndexItem {
 public:
  FrameIndexItem(s64 timestamp, u64 offset, bool isKeyframe)
      : timestamp(timestamp),
        offsetAndIsKeyframeFlag((offset & ~isKeyframeBit) | (isKeyframe ? isKeyframeBit : 0)) {}
  
  inline s64 GetTimestamp() const {
    return timestamp;
  }
  
  inline u64 GetOffset() const {
    return offsetAndIsKeyframeFlag & ~isKeyframeBit;
  }
  
  inline bool IsKeyframe() const {
    return (offsetAndIsKeyframeFlag & isKeyframeBit) != 0;
  }
  
 private:
  s64 timestamp;  // in nanoseconds
  u64 offsetAndIsKeyframeFlag;
  
  constexpr static u64 isKeyframeBit = static_cast<u64>(1) << 63;
};

/// An index of the frames in an XRVideo file, allowing to retrieve the frame that should be displayed at a given timestamp.
/// For very large files, we might want to load the video index only partially, or we might want to only store information
/// about every Xth frame (from which we can start searching for the following frames X+1, X+2, ...), to save space. This could be
/// implemented in this class.
class FrameIndex {
 public:
  /// Constructs an empty frame index.
  FrameIndex();
  
  FrameIndex(FrameIndex&& other) = default;
  FrameIndex& operator=(FrameIndex&& other) = default;
  
  FrameIndex(const FrameIndex& other) = delete;
  FrameIndex& operator=(const FrameIndex& other) = delete;
  
  /// Loads the index from an index chunk from the given file.
  /// The given XRVideo reader's file cursor must be at the start of the file's index chunk.
  bool CreateFromIndexChunk(XRVideoReader* reader);
  
  /// Removes all frame data from the index.
  void Clear();
  
  /// Adds a frame to the end of the frames vector.
  void PushFrame(s64 timestamp, u64 offset, bool isKeyframe);
  
  /// Sets the video end timestamp and end offset. Must be called exactly once after adding all frames.
  void PushVideoEnd(s64 endTimestamp, u64 endOffset);
  
  /// Performs a binary search to find the frame that should be displayed at the given timestamp.
  /// If the timestamp is out of the valid range for the video, returns -1.
  int FindFrameIndexForTimestamp(s64 timestamp) const;
  
  /// Given a frameIndex, finds the frame(s) that this frame depends on for display.
  /// The resulting frame indices are passed back to baseKeyframeIfNeeded and predecessorIfNeeded,
  /// or if such frames are not required to display frameIndex, then -1 is passed back there.
  /// Note that this function may return the same frame as base keyframe and as predecessor (if the
  /// frame after a keyframe is passed in as frameIndex).
  void FindDependencyFrames(int frameIndex, int* baseKeyframeIfNeeded, int* predecessorIfNeeded) const;
  
  /// Returns the index item for the given frame index.
  ///
  /// Note that the first frame in an XRVideo is always guaranteed to be a keyframe
  /// (if not, XRVideo::TakeAndOpen() returns failure).
  inline const FrameIndexItem& At(int frameIndex) const { return frames[frameIndex]; }
  
  inline s64 GetVideoStartTimestamp() const { return frames.front().GetTimestamp(); }
  inline s64 GetVideoEndTimestamp() const { return frames.back().GetTimestamp(); }
  
  inline int GetFrameCount() const { return frames.size() - 1; }
  
 private:
  /// Vector of frame items. Contains a dummy item at the end whose timestamp is set
  /// to the end timestamp of the last frame in the video, and whose offset is set to
  /// the end offset of the last frame in the video.
  vector<FrameIndexItem> frames;
};

}

