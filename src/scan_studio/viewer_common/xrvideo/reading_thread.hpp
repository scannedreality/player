#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/xrvideo_file.hpp"

#include "scan_studio/viewer_common/streaming_input_stream.hpp"
#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/util.hpp"

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/index.hpp"
#include "scan_studio/viewer_common/xrvideo/playback_state.hpp"
#include "scan_studio/viewer_common/xrvideo/video_thread.hpp"

namespace scan_studio {
using namespace vis;

/// State of asynchronous XRVideo loading.
/// Attention: The numerical values here must match those in plugins / other code in different languages!
enum class XRVideoAsyncLoadState {
  /// Asynchronous loading is in progress.
  /// In this state, the file metadata, index, and playback state *must not* be accessed by user code.
  Loading = 0,
  
  /// There was an error during asynchronous loading.
  /// The video thus cannot be displayed.
  Error = 1,
  
  /// Asynchronous loading has finished.
  /// Please note that even if a video is in this state, it still may not display a frame,
  /// since another asynchronous process (frame decoding) also has to run for this.
  Ready = 2,
};

template <typename FrameT>
class ReadingThread {
 public:
  inline void SetDecodedFrameCacheInitialized(bool initialized) {
    {
      lock_guard<mutex> decodedFrameCacheInitializedLock(decodedFrameCacheInitializedMutex);
      decodedFrameCacheInitialized = initialized;
    }
    
    if (initialized) {
      decodedFrameCacheInitializedCondition.notify_all();
    }
  }
  
  void StartThread(
      bool verboseDecoding,
      PlaybackState* playbackState,
      VideoThread<FrameT>* videoThread,
      DecodingThread<FrameT>* decodingThread,
      DecodedFrameCache<FrameT>* decodedFrameCache,
      atomic<XRVideoAsyncLoadState>* asyncLoadState,
      bool* hasMetadata,
      XRVideoMetadata* metadata,
      u16* textureWidth,
      u16* textureHeight,
      FrameIndex* frameIndex,
      XRVideoReader* reader) {
    if (thread.joinable()) { thread.join(); }
    
    this->verboseDecoding = verboseDecoding;
    this->playbackState = playbackState;
    this->videoThread = videoThread;
    this->decodingThread = decodingThread;
    this->decodedFrameCache = decodedFrameCache;
    this->asyncLoadState = asyncLoadState;
    this->hasMetadata = hasMetadata;
    this->metadata = metadata;
    this->textureWidth = textureWidth;
    this->textureHeight = textureHeight;
    this->frameIndex = frameIndex;
    this->reader = reader;
    
    threadRunning = true;
    quitRequested = false;
    abortCurrentFrames = false;
    currentlyReading = false;
    thread = std::thread(std::bind(&ReadingThread::ThreadMain, this));
  }
  
  void RequestThreadToExit() {
    if (playbackState == nullptr) {
      quitRequested = true;
      return;
    }
    
    {
      lock_guard<mutex> decodedFrameCacheInitializedLock(decodedFrameCacheInitializedMutex);
      lock_guard<mutex> playbackStateLock(playbackState->GetMutex());
      quitRequested = true;
      playbackState->GetPlaybackChangeCondition().notify_all();  // TODO: Does this notify have to happen while holding the locks?
    }
    
    decodedFrameCacheInitializedCondition.notify_all();
    
    // In case of streaming data over a network connection,
    // abort the streaming, even if reader->ReadNextFrame() stalled due to a very slow or dropped network connection.
    //
    // This is not implemented in an ideal way, but this seems like the best solution
    // for now without potentially complicating the process a lot.
    while (currentlyReading) {
      reader->AbortRead();
      this_thread::sleep_for(100us);
    }
  }
  
  bool IsThreadRunning() const {
    return threadRunning;
  }
  
  void WaitForThreadToExit() {
    RequestThreadToExit();
    if (thread.joinable()) {
      thread.join();
    }
  }
  
  /// Aborts reading the current range of frames (in case the thread is currently doing that).
  /// This is used when seeking. If this is called while the playbackState is locked,
  /// then after the function returns the reading thread will not read or queue up any further frames
  /// until the playbackState gets unlocked. If streaming is used, any pending scheduled streaming
  /// ranges will be cleared, too.
  void AbortCurrentFrames() {
    abortMutex.lock();
    abortCurrentFrames = true;
    abortMutex.unlock();
    
    if (reader->UsesStreamingInputStream()) {
      streamingMutex.lock();
      reader->GetStreamingInputStream()->DropPendingRequests();
      streamingMutex.unlock();
    }
  }
  
 private:
  void ThreadMain() {
    SCAN_STUDIO_SET_THREAD_NAME("scan-reading");
    
    // On startup, read the file's metadata and index
    currentlyReading = true;
    if (!ReadFileMetadataAndIndex()) {
      currentlyReading = false;
      *asyncLoadState = XRVideoAsyncLoadState::Error;
      threadRunning = false;
      return;
    }
    currentlyReading = false;
    
    // If caching all frames, then the decoded frames cache will only be resized (on the main thread) after the
    // frame count has been read asynchronously above (in ReadFileMetadataAndIndex()). To prevent race conditions,
    // we wait here for this resize to happen before using decodedFrameCache below.
    unique_lock<mutex> decodedFrameCacheInitializedLock(decodedFrameCacheInitializedMutex);
    while (!decodedFrameCacheInitialized && !quitRequested) {
      decodedFrameCacheInitializedCondition.wait(decodedFrameCacheInitializedLock);
    }
    decodedFrameCacheInitializedLock.unlock();
    
    // Main loop, reading frames as needed
    while (!quitRequested) {
      unique_lock<mutex> playbackStateLock(playbackState->GetMutex());
      
      vector<WriteLockedCachedFrame<FrameT>> lockedCacheItems = decodedFrameCache->LockCacheItemsForDecodingNextFrame(NextFramesIterator(playbackState, frameIndex), *frameIndex);
      
      // Check quitRequested while holding playbackStateLock to ensure
      // we catch it getting set to true by RequestThreadToExit() before possibly blocking below
      if (quitRequested) {
        break;
      }
      
      // If we got one or more locked cache items, decode the corresponding frames,
      // otherwise block until the situation changes.
      if (lockedCacheItems.empty()) {
        // If we are streaming data, then query some data in advance before going into the wait,
        // to get a larger pre-buffered region for being able to better handle unreliable network conditions.
        if (reader->UsesStreamingInputStream()) {
          NextFramesIterator nextPlayedFramesIt(playbackState, frameIndex);
          playbackStateLock.unlock();
          streamingMutex.lock();
          if (!abortCurrentFrames) {
            PreScheduleFramesForStreaming(nextPlayedFramesIt, *frameIndex);
          }
          streamingMutex.unlock();
          playbackStateLock.lock();
          if (quitRequested) {
            break;
          }
        }
        
        // TODO: This condition wakes up when the playback time changes, even if the new
        //       time is within the same frame as before the change. Perhaps we could
        //       detect this here (and whether the other playback state settings also remain constant)
        //       and in this case skip checking the whole decoded frame cache validity?
        // TODO: The reason for the used timeout in the wait_for() below is that playback time changes (that we are waiting for) are not the
        //       only event that makes more frames available for reading. More frames also become
        //       available if read locks to existing, no longer needed frames are dropped.
        //       This happens when read locks are deleted (delayed) for video frames rendered in previous
        //       render frames, or when the video playback latches on to the current frame
        //       while buffering (after buffering started because the current frame was not
        //       available in the cache). But, so far, we don't have a mechanism for these
        //       events to wake up the reading thread. Thus the automatic wake-up after a short
        //       duration. This wake-up is necessary to be able to completely fill the decoded frames cache during
        //       buffering if at the start of buffering some frames were unavailable due to existing read locks onto them.
        //       And if decoding is slow, a filled cache is the only condition that will resume playback.
        //       So, the timeout is ultimately necessary to avoid hanging playback due to buffering forever.
        //       The "to do" item here is that it would probably be nicer if read lock releasing would directly
        //       wake up the reading thread.
        playbackState->GetPlaybackChangeCondition().wait_for(playbackStateLock, 250ms);
      } else {
        abortCurrentFrames = false;
        playbackStateLock.unlock();
        
        ReadFramesForDecoding(move(lockedCacheItems));
      }
    }
    
    threadRunning = false;
  }
  
  bool ReadFileMetadataAndIndex() {
    // Load metadata, if present
    *hasMetadata = reader->ReadMetadata(metadata);
    if (quitRequested) { return false; }
    
    // Load the frame index from the index chunk, if present, or compile it from the frame data (slow)
    if (reader->FindNextChunk(xrVideoIndexChunkIdentifierV0)) {
      if (quitRequested) { return false; }
      
      if (!frameIndex->CreateFromIndexChunk(reader)) {
        LOG(ERROR) << "Reading the XRVideo file's index chunk failed";
        return false;
      }
    } else {
      // Go over all XRVideo frames in the file to create the index.
      LOG(WARNING) << "The opened file does not have an index chunk. Seeking over the whole file to build an index. This may be slow.";
      
      vector<u8> frameData;
      s64 lastFrameEndTimestamp = numeric_limits<s64>::lowest();
      
      frameIndex->Clear();
      reader->Seek(0);
      
      while (true) {
        // const TimePoint readStartTime = Clock::now();
        
        u64 frameOffsetInFile;
        if (!reader->ReadNextFrame(&frameData, &frameOffsetInFile)) {
          break;
        }
        
        // const TimePoint readEndTime = Clock::now();
        // LOG(1) << "Read frame data (" << frameData.size() << " bytes) from file in " << (MillisecondsFromTo(readStartTime, readEndTime)) << " ms";
        
        // Read the metadata
        const u8* dataPtr = frameData.data();
        XRVideoFrameMetadata frameMetadata;
        if (!XRVideoReadMetadata(&dataPtr, frameData.size(), &frameMetadata)) {
          LOG(ERROR) << "Reading XRVideo metadata failed";
          return false;
        }
        
        // Add the index item
        frameIndex->PushFrame(frameMetadata.startTimestamp, frameOffsetInFile, frameMetadata.isKeyframe);
        lastFrameEndTimestamp = frameMetadata.endTimestamp;
        
        if (quitRequested) { return false; }
      }
      
      frameIndex->PushVideoEnd(lastFrameEndTimestamp, reader->GetFileOffset());
    }
    
    // Sanity check: Ensure that we have at least one frame, and that the first frame is a keyframe.
    // Otherwise, fail loading.
    if (frameIndex->GetFrameCount() == 0) {
      LOG(ERROR) << "The XRVideo does not contain any frames.";
      return false;
    }
    if (!frameIndex->At(0).IsKeyframe()) {
      LOG(ERROR) << "The first frame in the XRVideo is not a keyframe.";
      return false;
    }
    
    // Peek into the first frame to read the video's texture size.
    // TODO: Once we update the XRV format, it would make sense to add a "maxTextureSize" attribute to the file header instead.
    reader->Seek(frameIndex->At(0).GetOffset());
    
    vector<u8> frameData;
    u64 frameOffsetInFile;
    if (!reader->ReadNextFrame(&frameData, &frameOffsetInFile)) {
      LOG(ERROR) << "The XRVideo does not contain any frames.";
      return false;
    }
    
    const u8* dataPtr = frameData.data();
    XRVideoFrameMetadata frameMetadata;
    if (!XRVideoReadMetadata(&dataPtr, frameData.size(), &frameMetadata)) {
      LOG(ERROR) << "Reading XRVideo metadata failed";
      return false;
    }
    
    *textureWidth = frameMetadata.textureWidth;
    *textureHeight = frameMetadata.textureHeight;
    
    // Initialize our playback state
    playbackState->SetPlaybackTimeRange(frameIndex->GetVideoStartTimestamp(), frameIndex->GetVideoEndTimestamp());
    playbackState->Seek(frameIndex->GetVideoStartTimestamp(), /*forward*/ true);
    
    // Signal that the video metadata has been loaded
    *asyncLoadState = XRVideoAsyncLoadState::Ready;
    return true;
  }
  
  void ReadFramesForDecoding(vector<WriteLockedCachedFrame<FrameT>>&& lockedCacheItems) {
    // We must decode frames sequentially, starting from a keyframe, due to the AV.1 texture video frames.
    // Thus, starting from the lowest frame index of the lockedCacheItems, we find its base keyframe by going back, in order
    // to find out where we have to start decoding to finally reach frameIndexToDecode.
    // If we encounter lastDecodedFrame + 1 on the way, we can start from there.
    const int decodingThreadLastFrameIndexQueuedForDecoding = decodingThread->GetLastFrameIndexQueuedForDecoding();
    const int videoThreadLastFrameIndexQueuedForDecoding = videoThread->GetLastFrameIndexQueuedForDecoding();
    const int successiveDecodingFrameIndex =
        (decodingThreadLastFrameIndexQueuedForDecoding == videoThreadLastFrameIndexQueuedForDecoding) ?
        (decodingThreadLastFrameIndexQueuedForDecoding + 1) : 0;
    if (verboseDecoding && decodingThreadLastFrameIndexQueuedForDecoding != videoThreadLastFrameIndexQueuedForDecoding) {
      LOG(WARNING) << "The last frames queued for decoding differ between the video and decoding threads. This should be rare, otherwise performance will be bad.";
    }
    
    int minFrameIndex = numeric_limits<int>::max();
    int maxFrameIndex = numeric_limits<int>::min();
    
    for (const WriteLockedCachedFrame<FrameT>& lockedFrame : lockedCacheItems) {
      minFrameIndex = min(minFrameIndex, lockedFrame.GetFrameIndex());
      maxFrameIndex = max(maxFrameIndex, lockedFrame.GetFrameIndex());
    }
    
    int startFrameIndex = minFrameIndex;
    while (startFrameIndex >= 0 &&
           startFrameIndex != successiveDecodingFrameIndex &&
           !frameIndex->At(startFrameIndex).IsKeyframe()) {
      -- startFrameIndex;
    }
    if (startFrameIndex < 0) {
      // This should never happen in theory, since the first frame should
      // always be guaranteed to be a keyframe.
      LOG(ERROR) << "Did not find any keyframe preceding frame " << minFrameIndex;
      for (auto& cacheItem : lockedCacheItems) {
        cacheItem.Invalidate();
      }
      return;
    }
    
    if (verboseDecoding) {
      LOG(1) << "ReadingThread: ReadFramesForDecoding() startFrameIndex: " << startFrameIndex << ", minFrameIndex: " << minFrameIndex << ", maxFrameIndex: " << maxFrameIndex;
    }
    
    // Read the frames in the range [startFrameIndex, maxFrameIndex] and queue them up for decoding.
    // Store the frames that we have cached frame items for, while discarding the others.
    // NOTE: This decoding strategy will not work properly with files played back backwards:
    //       Requesting a dependent frame that has many other dependent frames before it will
    //       require decoding all the other dependent frames, but throw them away, despite them
    //       being needed for rendering soon as well.
    int nextCacheItem = 0;
    
    auto invalidateFollowingCacheItems = [&nextCacheItem, &lockedCacheItems]() {
      for (; nextCacheItem < lockedCacheItems.size(); ++ nextCacheItem) {
        lockedCacheItems[nextCacheItem].Invalidate();
      }
    };
    
    for (int currentFrameIndex = startFrameIndex; currentFrameIndex <= maxFrameIndex; ++ currentFrameIndex) {
      const TimePoint readingStartTime = Clock::now();
      
      reader->Seek(frameIndex->At(currentFrameIndex).GetOffset());
      
      shared_ptr<vector<u8>> frameData(new vector<u8>());
      currentlyReading = true;
      if (quitRequested || !reader->ReadNextFrame(frameData.get())) {
        currentlyReading = false;
        if (!quitRequested) { LOG(ERROR) << "Failed to read XRVideo frame " << currentFrameIndex; }
        invalidateFollowingCacheItems();
        return;
      }
      currentlyReading = false;
      
      WriteLockedCachedFrame<FrameT>* cacheItem = nullptr;
      if (nextCacheItem < lockedCacheItems.size() && lockedCacheItems[nextCacheItem].GetFrameIndex() == currentFrameIndex) {
        cacheItem = &lockedCacheItems[nextCacheItem];
        ++ nextCacheItem;
      }
      
      const TimePoint readingEndTime = Clock::now();
      
      if (verboseDecoding) {
        LOG(1) << "ReadingThread: Read frame " << currentFrameIndex << " in " << MillisecondsFromTo(readingStartTime, readingEndTime) << " ms";
      }
      
      {
        lock_guard<mutex> abortLock(abortMutex);
        
        // After the part in the loop that is expected to take the most time (reading the data),
        // check whether any abort flag has been set.
        if (quitRequested || abortCurrentFrames) {
          if (verboseDecoding) { LOG(INFO) << "ReadingThread: abortCurrentFrames is set, aborting"; }
          if (cacheItem) { cacheItem->Invalidate(); } invalidateFollowingCacheItems(); break;
        }
        
        // Parse the frame's metadata since that is required for both the
        // video thread and the decoding thread (and it should not take long to do that).
        shared_ptr<XRVideoFrameMetadata> frameMetadata(new XRVideoFrameMetadata());
        
        const u8* frameContentPtr = frameData->data();
        if (!XRVideoReadMetadata(&frameContentPtr, frameData->size(), frameMetadata.get())) {
          LOG(ERROR) << "Reading XRVideo metadata failed";
          if (cacheItem) { cacheItem->Invalidate(); } invalidateFollowingCacheItems(); break;
        }
        
        // Push the frame data into the video thread's input queue.
        bool success = videoThread->QueueFrame(
            currentFrameIndex,
            frameMetadata,
            frameData,
            frameContentPtr);
        
        // Did a clear of the video thread's work queue make QueueFrame() fail due
        // to inconsistent video decoding state?
        if (!success) { if (cacheItem) { cacheItem->Invalidate(); } invalidateFollowingCacheItems(); break; }
        
        // Push the frame data into the decoding thread's input queue,
        // specifying the target cache item (if we need the frame and it's not in the cache yet)
        // or nullptr (if we only need to decode it to advance the decoding state because we don't need it at all, or it is already in the cache)
        success = decodingThread->QueueFrame(
            currentFrameIndex,
            frameMetadata,
            frameData,
            frameContentPtr,
            NanosecondsFromTo(readingStartTime, readingEndTime),
            cacheItem ? move(*cacheItem) : WriteLockedCachedFrame<FrameT>());
        
        // Did a clear of the decoding thread's work queue make QueueFrame() fail due
        // to inconsistent decoding state?
        if (!success) { invalidateFollowingCacheItems(); break; }
      }
    }
  }
  
  void PreScheduleFramesForStreaming(NextFramesIterator nextPlayedFramesIt, const FrameIndex& index) {
    // The number of seconds of video that we will try to buffer in advance
    const float secondsToBufferInAdvance = 5.f;
    const s64 nanosecondsToBufferInAdvance = SecondsToNanoseconds(secondsToBufferInAdvance);
    
    // The maximum number of frames that we iterate through to buffer data in advance
    // (intended to prevent this loop from taking too much time to run if the time criterion is not reached for some reason)
    const int maxLookaheadFrames = static_cast<int>(30 * secondsToBufferInAdvance + 0.5f);
    
    StreamingInputStream* streaming = reader->GetStreamingInputStream();
    
    s64 scheduleRangeFrom = -1;
    s64 scheduleRangeTo = -1;
    
    auto streamScheduledRange = [&scheduleRangeFrom, &scheduleRangeTo, &streaming]() {
      constexpr s64 maxStreamSize = 6 * 1024 * 1024;  // 6 MiB  // TODO: Put this parameter in the same location as minStreamSize
      streaming->StreamRange(scheduleRangeFrom, scheduleRangeTo, /*allowExtendRange*/ true, maxStreamSize);
    };
    
    s64 bufferedNanoseconds = 0;
    int lookaheadFrames = 0;
    
    while (!nextPlayedFramesIt.AtEnd()) {
      const int nextFrameIndex = *nextPlayedFramesIt;
      
      const auto& thisIndexItem = index.At(nextFrameIndex);
      const auto& nextIndexItem = index.At(nextFrameIndex + 1);
      
      const u64 frameRangeFrom = thisIndexItem.GetOffset();
      const u64 frameRangeTo = nextIndexItem.GetOffset() - 1;
      
      // Add the frame's data range to the scheduled frames.
      // Notice that we ignore the frames that this frame is dependent on here (this could be a keyframe and a previous frame)
      // since in almost all cases, their data should already be available or already be requested.
      if (scheduleRangeFrom < 0) {
        scheduleRangeFrom = frameRangeFrom;
        scheduleRangeTo = frameRangeTo;
      } else if (frameRangeFrom == scheduleRangeTo + 1) {
        scheduleRangeTo = frameRangeTo;
      } else if (frameRangeTo == scheduleRangeFrom - 1) {
        scheduleRangeFrom = frameRangeFrom;
      } else {
        streamScheduledRange();
        scheduleRangeFrom = frameRangeFrom;
        scheduleRangeTo = frameRangeTo;
      }
      
      bufferedNanoseconds += nextIndexItem.GetTimestamp() - thisIndexItem.GetTimestamp();
      if (bufferedNanoseconds >= nanosecondsToBufferInAdvance) {
        break;
      }
      
      ++ lookaheadFrames;
      if (lookaheadFrames >= maxLookaheadFrames) {
        break;
      }
      
      ++ nextPlayedFramesIt;
    }
    
    if (scheduleRangeFrom >= 0) {
      streamScheduledRange();
    }
  }
  
  // Worker thread
  atomic<bool> threadRunning;
  atomic<bool> quitRequested;
  atomic<bool> abortCurrentFrames;
  atomic<bool> currentlyReading;
  mutex abortMutex;
  mutex streamingMutex;
  std::thread thread;
  
  // Config
  bool verboseDecoding;
  
  // External state
  atomic<bool> decodedFrameCacheInitialized;
  mutex decodedFrameCacheInitializedMutex;
  condition_variable decodedFrameCacheInitializedCondition;
  
  // External object pointers (none of them is owned by this class)
  PlaybackState* playbackState = nullptr;
  VideoThread<FrameT>* videoThread;
  DecodingThread<FrameT>* decodingThread;
  DecodedFrameCache<FrameT>* decodedFrameCache;
  atomic<XRVideoAsyncLoadState>* asyncLoadState;
  bool* hasMetadata;
  XRVideoMetadata* metadata;
  u16* textureWidth;
  u16* textureHeight;
  FrameIndex* frameIndex;
  XRVideoReader* reader;
};

}
