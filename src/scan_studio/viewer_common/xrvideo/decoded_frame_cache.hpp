#pragma once

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <loguru.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/xrvideo/index.hpp"
#include "scan_studio/viewer_common/xrvideo/playback_state.hpp"

namespace scan_studio {
using namespace vis;

template <typename FrameT> class WriteLockedCachedFrame;
template <typename FrameT> class ReadLockedCachedFrame;
template <typename FrameT> class DecodedFrameCache;

template <typename FrameT>
struct DecodedFrameCacheItem {
 friend class WriteLockedCachedFrame<FrameT>;
 friend class ReadLockedCachedFrame<FrameT>;
 friend class DecodedFrameCache<FrameT>;
 private:
  inline bool HasValidData() const { return frameIndex >= 0; }
  
  inline bool IsWriteOrReadLocked() const { return isWriteLocked || readLockCount > 0; }
  
  /// The actual cached frame data
  FrameT frame;
  
  /// The frame index of the decoded frame, or -1 if this cache item does
  /// not contain valid data (see HasValidData()).
  int frameIndex = -1;
  
  /// Whether this cache item is currently locked for exclusive write access.
  /// Write locks are used by the frame reading and decoding threads.
  bool isWriteLocked = false;
  
  /// The number of currently held read locks for this cache item. Any number
  /// of read locks can be held concurrently, but no write lock can be obtained
  /// while at least one read lock is held.
  /// Read locks are used by rendering.
  int readLockCount = 0;
  
  constexpr static int maxDependencyCount = 2;
  
  /// The indices of the frames that this item depends on.
  /// Invalid entries are set to -1.
  /// Only valid if HasValidData() returns true.
  int dependsOnFrameIndices[maxDependencyCount];
};

/// Helper class returned by DecodedFrameCache's locking functions that automatically
/// locks the frame for writing on construction, and unlocks the frame again when it is destructed.
template <typename FrameT>
class WriteLockedCachedFrame {
 public:
  inline WriteLockedCachedFrame()
      : cacheItem(nullptr) {}
  
  ~WriteLockedCachedFrame() {
    Unlock();
  }
  
  inline WriteLockedCachedFrame(DecodedFrameCacheItem<FrameT>* cacheItem, int cacheItemIndex, DecodedFrameCache<FrameT>* cache)
      : cacheItem(cacheItem),
        cacheItemIndex(cacheItemIndex),
        cache(cache) {
    if (cache->framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked when constructing a WriteLockedCachedFrame"; }
    cacheItem->isWriteLocked = true;
  }
  
  WriteLockedCachedFrame(WriteLockedCachedFrame<FrameT>&& other)
      : cacheItem(other.cacheItem),
        cacheItemIndex(other.cacheItemIndex),
        cache(other.cache) {
    other.cacheItem = nullptr;
  }
  
  WriteLockedCachedFrame<FrameT>& operator= (WriteLockedCachedFrame<FrameT>&& other) {
    swap(cacheItem, other.cacheItem);
    swap(cacheItemIndex, other.cacheItemIndex);
    swap(cache, other.cache);
    
    return *this;
  }
  
  WriteLockedCachedFrame(const WriteLockedCachedFrame<FrameT>& other) = delete;
  WriteLockedCachedFrame<FrameT>& operator= (const WriteLockedCachedFrame<FrameT>& other) = delete;
  
  void Invalidate() {
    if (cacheItem != nullptr) {
      lock_guard<mutex> lock(cache->framesMutex);
      cache->InvalidateCacheItem(cacheItemIndex);
    }
  }
  
  void Unlock() {
    if (cacheItem != nullptr) {
      cache->ReleaseWriteLock(cacheItemIndex);
      cacheItem = nullptr;
    }
  }
  
  /// Returns the index of the locked frame.
  inline int GetFrameIndex() const { return cacheItem->frameIndex; }
  
  /// Returns the cache item index of the locked frame.
  inline int GetCacheItemIndex() const { return cacheItemIndex; }
  
  /// Returns a pointer to the locked frame struct.
  inline FrameT* GetFrame() const { return &cacheItem->frame; }
  
 private:
  DecodedFrameCacheItem<FrameT>* cacheItem;  // not owned
  int cacheItemIndex;
  DecodedFrameCache<FrameT>* cache;  // not owned
};

/// Helper class returned by DecodedFrameCache's locking functions that automatically
/// locks the frame for reading on construction, and unlocks the frame again when it is destructed.
template <typename FrameT>
class ReadLockedCachedFrame {
 public:
  inline ReadLockedCachedFrame()
      : cacheItem(nullptr) {}
  
  ~ReadLockedCachedFrame() {
    Unlock();
  }
  
  inline ReadLockedCachedFrame(DecodedFrameCacheItem<FrameT>* cacheItem, int cacheItemIndex, DecodedFrameCache<FrameT>* cache)
      : cacheItem(cacheItem),
        cacheItemIndex(cacheItemIndex),
        cache(cache) {
    if (cache->framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked when constructing a ReadLockedCachedFrame"; }
    ++ cacheItem->readLockCount;
  }
  
  ReadLockedCachedFrame(ReadLockedCachedFrame<FrameT>&& other)
      : cacheItem(other.cacheItem),
        cacheItemIndex(other.cacheItemIndex),
        cache(other.cache) {
    other.cacheItem = nullptr;
  }
  
  ReadLockedCachedFrame<FrameT>& operator= (ReadLockedCachedFrame<FrameT>&& other) {
    swap(cacheItem, other.cacheItem);
    swap(cacheItemIndex, other.cacheItemIndex);
    swap(cache, other.cache);
    
    return *this;
  }
  
  ReadLockedCachedFrame(const ReadLockedCachedFrame<FrameT>& other)
      : cacheItem(other.cacheItem),
        cacheItemIndex(other.cacheItemIndex),
        cache(other.cache) {
    if (cache->framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked when copy-constructing a ReadLockedCachedFrame"; }
    ++ cacheItem->readLockCount;
  }
  
  ReadLockedCachedFrame<FrameT>& operator= (const ReadLockedCachedFrame<FrameT>& other) = delete;
  
  void Unlock() {
    if (cacheItem != nullptr) {
      cache->ReleaseReadLock(cacheItemIndex);
      cacheItem = nullptr;
    }
  }
  
  /// Returns the index of the locked frame.
  inline int GetFrameIndex() const { return cacheItem->frameIndex; }
  
  /// Returns the cache item index of the locked frame.
  inline int GetCacheItemIndex() const { return cacheItemIndex; }
  
  /// Returns a pointer to the locked frame struct.
  /// TODO: It would be nicer to return a "const FrameT*" here, since this is supposed to be a read lock.
  ///       However, the Vulkan render path currently needs to change state in its render functions.
  inline FrameT* GetFrame() const { return &cacheItem->frame; }
  
 private:
  DecodedFrameCacheItem<FrameT>* cacheItem;  // not owned
  int cacheItemIndex;
  DecodedFrameCache<FrameT>* cache;  // not owned
};

/// Provides a place where decoded frames are cached until they are needed for rendering.
///
/// Note that for rendering a frame, up to two other frames may need to be cached:
/// * Its direct predecessor frame (to access its deformation state)
/// * Its base keyframe (to access its mesh)
template <typename FrameT>
class DecodedFrameCache {
 friend class WriteLockedCachedFrame<FrameT>;
 friend class ReadLockedCachedFrame<FrameT>;
 public:
  /// Creates a decoded frame cache that can store the given number of frames.
  bool Initialize(int capacity) {
    cache.resize(capacity);
    return true;
  }
  
  void Destroy() {
    cache = vector<DecodedFrameCacheItem<FrameT>>();
    frameIndexToCacheItemIndex = unordered_map<int, int>();
  }
  
  /// Invalidates all cache items. This is useful when switching playback to another video:
  /// InvalidateAllCacheItems() can be used to invalidate all cached frames of the old video.
  void InvalidateAllCacheItems() {
    lock_guard<mutex> lock(framesMutex);
    
    for (int cacheItemIndex = 0, size = cache.size(); cacheItemIndex < size; ++ cacheItemIndex) {
      InvalidateCacheItem(cacheItemIndex);
    }
  }
  
  /// For debugging, prints the frame indices of all cached frames that are write or read locked.
  void DebugPrintCacheHealth() {
    lock_guard<mutex> lock(framesMutex);
    
    LOG(1) << "-- cache health start (showing only held locks) --";
    
    int entriesWithValidData = 0;
    
    for (int cacheItemIndex = 0, size = cache.size(); cacheItemIndex < size; ++ cacheItemIndex) {
      auto& cacheItem = cache[cacheItemIndex];
      
      if (cacheItem.HasValidData()) {
        ++ entriesWithValidData;
        
        if (cacheItem.isWriteLocked) {
          LOG(1) << "- write locked: " << cacheItem.frameIndex << " (deps: " << cacheItem.dependsOnFrameIndices[0] << ", " << cacheItem.dependsOnFrameIndices[1] << ")";
        }
        if (cacheItem.readLockCount > 0) {
          LOG(1) << "- read locked: " << cacheItem.frameIndex << " (deps: " << cacheItem.dependsOnFrameIndices[0] << ", " << cacheItem.dependsOnFrameIndices[1] << ")";
        }
      }
    }
    
    LOG(1) << "";
    LOG(1) << "entries with valid data: " << entriesWithValidData << " / " << cache.size();
    LOG(1) << "-- cache health end  --";
  }
  
  /// High-level: Finds and locks cache item(s) to decode the next frame required for playback.
  ///
  /// In detail:
  /// `nextPlayedFramesIt` is a frames iterator that is set to the current video playback state.
  /// The function uses this iterator to find the next frames that will be played back
  /// (assuming that the user will not influence the playback, e.g., by seeking).
  ///
  /// For each such frame, the function checks whether the frame is in the cache, as well as
  /// the frames they may depend on (keyframe, predecessor frame).
  /// If so, these cache items are flagged as 'required'.
  ///
  /// If all of the cached frames get flagged as required, the function stops,
  /// since there is currently no other frame that should get decoded. However,
  /// if the frames iterator yields a frame that is not in the cache (or a frame whose
  /// dependency/ies are not in the cache) before all cached frames got flagged as required,
  /// then the function tries to find space in the cache to decode this frame and/or its
  /// dependency-frames (keyframe, predecessor frame).
  ///
  /// To do so, the function goes over the non-requried-flagged items in the cache and
  /// selects the cache items that are:
  /// * Not locked
  /// * Expected to be shown last, given the current playback state
  /// If sufficient such cache items exist, locks and returns them.
  /// GetFrameIndex() should be called on the returned locked cache items to know which frames to
  /// decode into them. The locked cache items are always returned in order of increasing frame index,
  /// and it always holds that all returned frame indices have the same base keyframe.
  ///
  /// If the cache is filled with 'required' frames, or does not have space for the items needed
  /// to decode a new frame, the function returns an empty vector.
  vector<WriteLockedCachedFrame<FrameT>> LockCacheItemsForDecodingNextFrame(
      const NextFramesIterator& nextPlayedFramesIt,
      const FrameIndex& index) {
    int frameIndexToDecode = -1;
    
    NextFramesIterator it = nextPlayedFramesIt;
    
    int requiredFrameCount = 0;
    vector<u8> cacheItemIsRequired(cache.size(), 0);
    
    // Lock as late as possible
    lock_guard<mutex> lock(framesMutex);
    
    // Loop over the next frames, flagging the required cached frames
    while (!it.AtEnd()) {
      const int nextFrameIndex = *it;
      
      // Search for nextFrameIndex in the cache.
      //
      // Note that regarding requiredFrameCount counting, we currently don't check for the case
      // where the frame iterator returns some frames multiple times (this happens with back-and-forth playback).
      // In this case, we will count these frames as 'required' multiple times.
      // However, this should not really be relevant, and importantly protects us from a possible
      // endless loop here in case the iterator always returns the same frame(s) again without
      // setting AtEnd() to true.
      //
      // We do check for multiple 'required' counting for frame dependencies, since it
      // is a standard case that many dependent frames depend on the same keyframe.
      auto nextFrameIt = frameIndexToCacheItemIndex.find(nextFrameIndex);
      if (nextFrameIt == frameIndexToCacheItemIndex.end()) {
        // The frame with index `nextFrameIndex` is missing. Aim to decode it.
        frameIndexToDecode = nextFrameIndex;
        break;
      }
      
      // The frame with index `nextFrameIndex` is already in the cache.
      // Mark this cache item and its dependencies as required.
      const int cacheIndex = nextFrameIt->second;
      
      // (Note that we are on purpose not checking if cacheItemIsRequired is already set here, see the comment above.)
      ++ requiredFrameCount;
      cacheItemIsRequired[cacheIndex] = 1;
      
      // Check for the dependencies of the frame.
      // If they are in the cache, mark them as required as well.
      // If any dependency is not in the cache, aim to decode it.
      for (int i = 0; i < DecodedFrameCacheItem<FrameT>::maxDependencyCount; ++ i) {
        const int dependencyFrameIndex = cache[cacheIndex].dependsOnFrameIndices[i];
        if (dependencyFrameIndex >= 0) {
          auto dependencyFrameIt = frameIndexToCacheItemIndex.find(dependencyFrameIndex);
          
          if (dependencyFrameIt != frameIndexToCacheItemIndex.end()) {
            // The dependency is cached.
            const int dependencyCacheIndex = dependencyFrameIt->second;
            if (!cacheItemIsRequired[dependencyCacheIndex]) {
              ++ requiredFrameCount;
              cacheItemIsRequired[dependencyCacheIndex] = 1;
            }
          } else {
            // The dependency is missing.
            frameIndexToDecode = nextFrameIndex;
            break;
          }
        }
      }
      
      if (frameIndexToDecode != -1) {
        // We need to decode a missing dependency of frameIndexToDecode.
        break;
      }
      
      if (requiredFrameCount >= cache.size()) {
        // All frames in the cache are flagged as required.
        return {};
      }
      
      ++ it;
    }
    
    if (frameIndexToDecode == -1) {
      // Reached the iterator's end without encountering a missing frame.
      return {};
    }
    
    // Find suitable cache structs to decode the frame into,
    // as described in the comment on this function (LockCacheItemsForDecodingNextFrame()).
    // First, determine the frames that frameIndexToDecode depends on.
    int baseKeyframe, predecessor;
    index.FindDependencyFrames(frameIndexToDecode, &baseKeyframe, &predecessor);
    
    // Check whether the frames are already cached,
    // or whether we need to lock cache items for them.
    int frameIndexToDecodeIfNeeded = (/*frameIndexToDecode >= 0 &&*/ !FrameIsCached(frameIndexToDecode)) ? frameIndexToDecode : -1;
    int baseKeyframeIfNeeded = (baseKeyframe >= 0 && !FrameIsCached(baseKeyframe)) ? baseKeyframe : -1;
    int predecessorIfNeeded = (predecessor >= 0 && predecessor != baseKeyframe && !FrameIsCached(predecessor)) ? predecessor : -1;
    
    // Try to obtain a cache item for each frame that needs to be decoded.
    // TODO: We don't need to stop entirely if we find cache space for some, but not all frames.
    //       We could already start decoding them partially (in the right order: with increasing frame index, starting from the keyframe).
    auto findGoodFreeCacheItem = [&]() {
      int longestDurationTillFrame = numeric_limits<int>::min();
      int selectedCacheIndex = -1;
      
      for (int cacheIndex = 0, size = cache.size(); cacheIndex < size; ++ cacheIndex) {
        DecodedFrameCacheItem<FrameT>& cacheItem = cache[cacheIndex];
        
        if (!cacheItemIsRequired[cacheIndex] && !cacheItem.IsWriteOrReadLocked()) {
          // This cache item is available. Compute the duration from the current playback frame
          // to this frame (measured in frames).
          const int durationTillFrame =
              cacheItem.HasValidData() ?
              nextPlayedFramesIt.ComputeDurationToFrame(cacheItem.frameIndex) :
              numeric_limits<int>::max();
          
          if (durationTillFrame > longestDurationTillFrame) {
            longestDurationTillFrame = durationTillFrame;
            selectedCacheIndex = cacheIndex;
          }
        }
      }
      
      return selectedCacheIndex;
    };
    
    int baseKeyframeCacheItem = -1;
    if (baseKeyframeIfNeeded >= 0) {
      baseKeyframeCacheItem = findGoodFreeCacheItem();
      if (baseKeyframeCacheItem < 0) {
        // No free cache item for the base keyframe
        return {};
      }
      cache[baseKeyframeCacheItem].isWriteLocked = true;  // exclude this item from the searches below
    }
    
    int predecessorCacheItem = -1;
    if (predecessorIfNeeded >= 0) {
      predecessorCacheItem = findGoodFreeCacheItem();
      if (predecessorCacheItem < 0) {
        // No free cache item for the predecessor keyframe
        if (baseKeyframeIfNeeded >= 0) { cache[baseKeyframeCacheItem].isWriteLocked = false; }
        return {};
      }
      cache[predecessorCacheItem].isWriteLocked = true;  // exclude this item from the searches below
    }
    
    int frameToDecodeCacheItem = -1;
    if (frameIndexToDecodeIfNeeded >= 0) {
      frameToDecodeCacheItem = findGoodFreeCacheItem();
      if (frameToDecodeCacheItem < 0) {
        // No free cache item for the frame to decode
        if (baseKeyframeIfNeeded >= 0) { cache[baseKeyframeCacheItem].isWriteLocked = false; }
        if (predecessorIfNeeded >= 0) { cache[predecessorCacheItem].isWriteLocked = false; }
        return {};
      }
      // cache[frameToDecodeCacheItem].isWriteLocked = true;  // no need for this, since we do not do further searches below
    }
    
    // We succeeded in getting enough cache items.
    // Delete their old content, and set them up for decoding the frame(s).
    if (baseKeyframeIfNeeded >= 0) {
      ConfigureCacheItem(baseKeyframeCacheItem, baseKeyframeIfNeeded, /*dependencyCount*/ 0, /*dependencyFrameIndices*/ nullptr);
    }
    
    if (predecessorIfNeeded >= 0) {
      const int predecessorDependencies[2] = {baseKeyframe, (predecessor - 1 != baseKeyframe) ? (predecessor - 1) : -1};
      const int predecessorDependencyCount = 1 + (predecessor - 1 != baseKeyframe);
      ConfigureCacheItem(predecessorCacheItem, predecessorIfNeeded, predecessorDependencyCount, predecessorDependencies);
    }
    
    if (frameIndexToDecodeIfNeeded >= 0) {
      const int frameToDecodeDependencies[2] = {baseKeyframe, (predecessor != baseKeyframe) ? predecessor : -1};
      const int frameToDecodeDependencyCount = (baseKeyframe >= 0) + (predecessor >= 0 && predecessor != baseKeyframe);
      ConfigureCacheItem(frameToDecodeCacheItem, frameIndexToDecode, frameToDecodeDependencyCount, frameToDecodeDependencies);
    }
    
    // Return the locked frames
    vector<WriteLockedCachedFrame<FrameT>> lockedFrames;
    lockedFrames.reserve(3);
    if (baseKeyframeIfNeeded >= 0) {
      lockedFrames.emplace_back(&cache[baseKeyframeCacheItem], baseKeyframeCacheItem, this);
    }
    if (predecessorIfNeeded >= 0) {
      lockedFrames.emplace_back(&cache[predecessorCacheItem], predecessorCacheItem, this);
    }
    if (frameIndexToDecodeIfNeeded >= 0) {
      lockedFrames.emplace_back(&cache[frameToDecodeCacheItem], frameToDecodeCacheItem, this);
    }
    return lockedFrames;
  }
  
  /// Tries to lock and return the cache items for the given frames (in the same order as passed in).
  /// If at least one of the frames is not in the cache or is write-locked, returns an empty vector.
  vector<ReadLockedCachedFrame<FrameT>> LockFramesForReading(const vector<int>& frameIndices) {
    lock_guard<mutex> lock(framesMutex);
    
    vector<int> cacheItemIndices(frameIndices.size());
    for (int i = 0, size = frameIndices.size(); i < size; ++ i) {
      auto it = frameIndexToCacheItemIndex.find(frameIndices[i]);
      if (it == frameIndexToCacheItemIndex.end() ||
          cache[it->second].isWriteLocked) {
        return {};
      }
      cacheItemIndices[i] = it->second;
    }
    
    vector<ReadLockedCachedFrame<FrameT>> lockedFrames;
    lockedFrames.reserve(frameIndices.size());
    for (int i = 0, size = frameIndices.size(); i < size; ++ i) {
      const int cacheItemIndex = cacheItemIndices[i];
      lockedFrames.emplace_back(&cache[cacheItemIndex], cacheItemIndex, this);
    }
    return lockedFrames;
  }
  
  /// Tries to lock and return the cache item with the given index.
  /// If the item is already locked, returns a null WriteLockedCachedFrame.
  inline WriteLockedCachedFrame<FrameT> LockCacheItemForWriting(int cacheItemIndex) {
    lock_guard<mutex> lock(framesMutex);
    
    if (cache[cacheItemIndex].IsWriteOrReadLocked()) {
      return {};
    } else {
      return WriteLockedCachedFrame<FrameT>(&cache[cacheItemIndex], cacheItemIndex, this);
    }
  }
  
  /// Checks the current decoding progress: Counts how many frames could be displayed,
  /// starting from the given frames iterator position, with the currently cached data.
  /// Also determines the time range of these frames, returned in readyFramesStartTime
  /// and readyFramesEndTime. This function checks at most the next cache.size() number
  /// of frames returned by the iterator.
  void CheckDecodingProgress(
      const NextFramesIterator& nextPlayedFramesIt,
      int* requiredFramesCount,
      int* readyFramesCount,
      s64* readyFramesStartTime,
      s64* readyFramesEndTime) {
    *requiredFramesCount = 0;  // note that this is counted differently here than in LockCacheItemsForDecodingNextFrame(): here, we do not do any duplicate counting.
    *readyFramesCount = 0;
    *readyFramesStartTime = numeric_limits<s64>::max();
    *readyFramesEndTime = numeric_limits<s64>::lowest();
    
    NextFramesIterator it = nextPlayedFramesIt;
    
    vector<u8> cacheItemIsRequired(cache.size(), 0);
    
    // Lock as late as possible
    lock_guard<mutex> lock(framesMutex);
    
    // Loop over the next frames
    while (!it.AtEnd()) {
      const int nextFrameIndex = *it;
      
      // Search for nextFrameIndex and the frames it depends on in the cache.
      bool frameIsInCacheAndReady = true;
      
      auto nextFrameIt = frameIndexToCacheItemIndex.find(nextFrameIndex);
      if (nextFrameIt == frameIndexToCacheItemIndex.end() ||
          cache[nextFrameIt->second].isWriteLocked) {
        break;
      }
      if (!cacheItemIsRequired[nextFrameIt->second]) {
        ++ *requiredFramesCount;
        cacheItemIsRequired[nextFrameIt->second] = 1;
      }
      
      for (int i = 0; i < DecodedFrameCacheItem<FrameT>::maxDependencyCount; ++ i) {
        const int dependencyFrameIndex = cache[nextFrameIt->second].dependsOnFrameIndices[i];
        if (dependencyFrameIndex >= 0) {
          auto dependencyFrameIt = frameIndexToCacheItemIndex.find(dependencyFrameIndex);
          if (dependencyFrameIt == frameIndexToCacheItemIndex.end() ||
              cache[dependencyFrameIt->second].isWriteLocked) {
            frameIsInCacheAndReady = false;
            break;
          }
          if (!cacheItemIsRequired[dependencyFrameIt->second]) {
            ++ *requiredFramesCount;
            cacheItemIsRequired[dependencyFrameIt->second] = 1;
          }
        }
      }
      
      if (!frameIsInCacheAndReady) {
        break;
      }
      
      // The frame with nextFrameIndex is in the cache and ready (i.e., not write-locked).
      DecodedFrameCacheItem<FrameT>& nextFrameItem = cache[nextFrameIt->second];
      
      ++ *readyFramesCount;
      *readyFramesStartTime = min(*readyFramesStartTime, nextFrameItem.frame.GetMetadata().startTimestamp);
      *readyFramesEndTime = max(*readyFramesEndTime, nextFrameItem.frame.GetMetadata().endTimestamp);
      
      if (*readyFramesCount >= cache.size()) {
        // Don't continue checking, since that might create an endless loop (if the frames
        // iterator returns the same frame(s) over and over again).
        return;
      }
      
      ++ it;
    }
  }
  
  /// Returns the number of decoded frames that may be stored in this cache.
  inline int GetCapacity() const {
    return cache.size();
  }
  
  /// Manually locks the frame cache (which allows copy-constructing read locks).
  inline void Lock() { framesMutex.lock(); }
  
  /// Manually unlocks the frame cache.
  inline void Unlock() { framesMutex.unlock(); }
  
 private:
  void ReleaseWriteLock(int cacheItemIndex) {
    lock_guard<mutex> lock(framesMutex);
    cache[cacheItemIndex].isWriteLocked = false;
  }
  
  void ReleaseReadLock(int cacheItemIndex) {
    lock_guard<mutex> lock(framesMutex);
    -- cache[cacheItemIndex].readLockCount;
  }
  
  /// Calling this function requires framesMutex to be locked.
  void InvalidateCacheItem(int cacheItemIndex) {
    if (framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked in call to InvalidateCacheItem()"; }
    auto& cacheItem = cache[cacheItemIndex];
    
    if (cacheItem.frameIndex >= 0) {
      frameIndexToCacheItemIndex.erase(cacheItem.frameIndex);
    }
    cacheItem.frameIndex = -1;
  }
  
  /// Calling this function requires framesMutex to be locked.
  void ConfigureCacheItem(int cacheItemIndex, int frameIndex, int dependencyCount, const int* dependencyFrameIndices) {
    if (framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked in call to ConfigureCacheItem()"; }
    auto& cacheItem = cache[cacheItemIndex];
    
    InvalidateCacheItem(cacheItemIndex);
    
    cacheItem.frameIndex = frameIndex;
    frameIndexToCacheItemIndex[frameIndex] = cacheItemIndex;
    
    if (dependencyCount > 0) {
      memcpy(cacheItem.dependsOnFrameIndices, dependencyFrameIndices, dependencyCount * sizeof(int));
    }
    for (int i = dependencyCount; i < DecodedFrameCacheItem<FrameT>::maxDependencyCount; ++ i) {
      cacheItem.dependsOnFrameIndices[i] = -1;
    }
  }
  
  /// Calling this function requires framesMutex to be locked.
  bool FrameIsCached(int frameIndex) {
    if (framesMutex.try_lock()) { LOG(ERROR) << "framesMutex was not locked in call to FrameIsCached()"; }
    return frameIndexToCacheItemIndex.find(frameIndex) != frameIndexToCacheItemIndex.end();
  }
  
  mutex framesMutex;
  
  /// An unordered vector of all cache items.
  /// Does not change size after the DecodedFrameCache's initialization
  /// (i.e., cache.size() may be called without locking framesMutex).
  vector<DecodedFrameCacheItem<FrameT>> cache;
  
  /// A map from frame index to cache item index to speed up checking whether
  /// a certain frame is in the cache.
  unordered_map<int, int> frameIndexToCacheItemIndex;
};

}

