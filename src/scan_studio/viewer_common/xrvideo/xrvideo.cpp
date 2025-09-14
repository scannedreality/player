#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

#include <cmath>
#include <cstring>
#include <memory>

#include <zstd.h>

#include <dav1d/dav1d.h>

#include <loguru.hpp>

#include <libvis/io/input_stream.h>

#include "scan_studio/common/xrvideo_file.hpp"

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/timing.hpp"

namespace scan_studio {

constexpr bool kVerbose = false;

constexpr float bufferingDurationThresholdInSeconds = 0.1f;

void XRVideo::SetExternalFrameResourcesCallbacks(std::function<bool(int, void*)> allocateCallback, std::function<void()> releaseAllCallback) {
  allocateExternalFrameResourcesCallback = allocateCallback;
  releaseAllExternalFrameResourcesCallback = releaseAllCallback;
}

bool XRVideo::Initialize(int cachedDecodedFrameCount, bool verboseDecoding, XRVideoCommonResources* commonResources) {
  this->cachedDecodedFrameCount = cachedDecodedFrameCount;
  this->verboseDecoding = verboseDecoding;
  this->commonResources = commonResources;
  
  if (!InitializeImpl()) { Destroy(); return false; }
  
  return true;
}

bool XRVideo::TakeAndOpen(InputStream* videoInputStream, bool isStreamingInputStream, bool cacheAllFrames) {
  // TODO: If this is called while a video is already being loaded, is there a chance that
  //       the ReadingThread modifies asyncLoadState after the assignment below, but before it
  //       switches to the new file?
  asyncLoadState = XRVideoAsyncLoadState::Loading;
  
  if (reader.IsOpen()) {
    // Initiate a delayed switch to the new video (see the comment on TakeAndOpen()).
    RequestLoadingThreadsToExit();
    nextInputStream.reset(videoInputStream);
    nextInputStreamIsStreamingInputStream = isStreamingInputStream;
    nextCacheAllFrames = cacheAllFrames;
    return true;
  }
  
  return TakeAndOpenImpl(videoInputStream, isStreamingInputStream, cacheAllFrames);
}

s64 XRVideo::Update(s64 elapsedNanoseconds) {
  constexpr s64 kErrorAndPreLoadReturnValue = numeric_limits<s64>::lowest();

  if (!reader.IsOpen()) {
    return kErrorAndPreLoadReturnValue;
  }
  
  // If a switch to another video is queued and the loading threads finished exiting, execute the switch.
  if (nextInputStream && AreLoadingThreadsExited()) {
    SwitchToNextInputStream();
  }
  
  // If async loading is in progress, buffer and don't access the video metadata, index, or playback state.
  if (asyncLoadState != XRVideoAsyncLoadState::Ready) {
    if (!isBuffering) { StartBuffering(); }
    bufferingProgressPercent = 0;
    if (!showBufferingIndicator) {
      const float bufferingDuration = SecondsDuration(Clock::now() - bufferingStartTime).count();
      showBufferingIndicator = (bufferingDuration >= bufferingDurationThresholdInSeconds);
    }
    return kErrorAndPreLoadReturnValue;
  }
  
  // If async loading for a new video has finished and the decoded frame cache has not been allocated yet, do this now.
  // (This has to happen on the main thread for the Unity plugin, and in knowledge of the video's texture size.)
  if (GetCacheCapacity() == 0) {
    if (!ResizeDecodedFrameCache(cacheAllFrames ? Index().GetFrameCount() : cachedDecodedFrameCount)) {
      LOG(ERROR) << "Failed to allocate video frames";
      return kErrorAndPreLoadReturnValue;
    }
    SetDecodedFrameCacheInitialized(true);
  }
  
  // If we were buffering, check whether enough frames were decoded so we can stop doing that
  if (isBuffering && !ShouldBuffer()) {
    StopBuffering();
  }
  
  // Update the playback time if not buffering
  s64 playbackTime;
  if (isBuffering || elapsedNanoseconds == 0) {
    playbackState.Lock();
    playbackTime = playbackState.GetPlaybackTime();
    playbackState.Unlock();
  } else {
    playbackTime = playbackState.Advance(elapsedNanoseconds);
  }
  
  // Determine the current frame index from the playback time
  const int currentFrameIndex = index.FindFrameIndexForTimestamp(playbackTime);
  if (currentFrameIndex < 0 || currentFrameIndex >= index.GetFrameCount()) {
    LOG(ERROR) << "The current playback time (" << playbackTime << ") did not yield a valid frame index";
    return playbackTime;
  }
  
  // Determine the frames that the current frame depends on
  int baseKeyframeIfNeeded = -1;
  int predecessorIfNeeded = -1;
  index.FindDependencyFrames(currentFrameIndex, &baseKeyframeIfNeeded, &predecessorIfNeeded);
  
  // Attempt to lock all required frames for rendering in the decoded frames cache.
  // Note that in case we try to lock the same frames again that are already locked,
  // LockFramesForRendering() will succeed, because frames may be read-locked multiple times simultaneously.
  vector<int> frameIndicesForRendering = {currentFrameIndex};
  if (baseKeyframeIfNeeded >= 0) { frameIndicesForRendering.push_back(baseKeyframeIfNeeded); }
  if (predecessorIfNeeded >= 0 && predecessorIfNeeded != baseKeyframeIfNeeded) { frameIndicesForRendering.push_back(predecessorIfNeeded); }
  
  if (LockFramesForRendering(frameIndicesForRendering)) {
    // Compute currentIntraFrameTime
    const s64 frameStartTime = index.At(currentFrameIndex).GetTimestamp();
    const s64 frameEndTime = index.At(currentFrameIndex + 1).GetTimestamp();  // always valid since there is a dummy item at the end of the frames index with the last frame's end time
    if (playbackTime < frameStartTime || playbackTime > frameEndTime) {
      LOG(ERROR) << "Internal logic error: playbackTime is not within the timestamp bounds of the current frame";
    }
    currentIntraFrameTime = max(0., min(1.0, (playbackTime - frameStartTime) / (1.0 * (frameEndTime - frameStartTime))));
  } else if (!isBuffering) {
    // The cache items we need for rendering could not be locked. Start buffering to wait
    // for a good number of follow-up frames to get decoded.
    if (kVerbose) {
      LOG(INFO) << "Starting buffering (failed to lock cache items for rendering; frames: " << currentFrameIndex << ", " << baseKeyframeIfNeeded << ", " << predecessorIfNeeded << ")";
    }
    StartBuffering();
  }
  
  return playbackTime;
}

void XRVideo::Seek(s64 timestamp, bool forward) {
  if (!reader.IsOpen()) { return; }
  
  // Lock the playback state such that the reading thread will not immediately read
  // outdated frames again after we call AbortCurrentFrames() on it.
  playbackState.Lock();
  
  // Abort frame reading and clear the frame decoding thread queue from already-read, but not decoded, frames.
  // TODO: We should keep those frames in the queues that happen to be still relevant for the seeked-to video time, if any.
  ClearLoadingThreadWorkQueues();
  
  playbackState.Seek(timestamp, forward);
  
  playbackState.Unlock();
  
  // If an insufficient number of frames to display is cached after seeking, go into buffering state.
  if (!isBuffering && ShouldBuffer()) {
    if (kVerbose) {
      LOG(INFO) << "Starting buffering (too few frames ready after seeking)";
    }
    StartBuffering();
  }
}

bool XRVideo::IsCurrentFrameDisplayReady() {
  if (!SwitchedToMostRecentVideo()) {
    return false;
  }
  
  playbackState.Lock();
  const s64 playbackTime = playbackState.GetPlaybackTime();
  playbackState.Unlock();
  
  // TODO: The code below is duplicated from Update(), should it be de-duplicated?
  
  // Determine the current frame index from the playback time
  const int currentFrameIndex = index.FindFrameIndexForTimestamp(playbackTime);
  if (currentFrameIndex < 0 || currentFrameIndex >= index.GetFrameCount()) {
    LOG(ERROR) << "The current playback time (" << playbackTime << ") did not yield a valid frame index";
    return true;
  }
  
  // Determine the frames that the current frame depends on
  int baseKeyframeIfNeeded = -1;
  int predecessorIfNeeded = -1;
  index.FindDependencyFrames(currentFrameIndex, &baseKeyframeIfNeeded, &predecessorIfNeeded);
  
  // Attempt to lock all required frames for rendering in the decoded frames cache
  vector<int> frameIndicesForRendering = {currentFrameIndex};
  if (baseKeyframeIfNeeded >= 0) { frameIndicesForRendering.push_back(baseKeyframeIfNeeded); }
  if (predecessorIfNeeded >= 0 && predecessorIfNeeded != baseKeyframeIfNeeded) { frameIndicesForRendering.push_back(predecessorIfNeeded); }
  
  // Note that in case we try to lock the same frames again that are already locked,
  // LockFramesForRendering() will succeed, because frames may be read-locked multiple times simultaneously.
  // Also, there is no need to manually unlock anything, since the frames locked for rendering are kept track of internally.
  return LockFramesForRendering(frameIndicesForRendering);
}

bool XRVideo::ShouldBuffer() {
  // We start running if both:
  // 1) A minimum number of follow-up frames got decoded.
  //    Having a minimum number of frames:
  //    * Helps in being able to better judge the average decoding speed.
  //    * Is useful to be able to smooth out decoding time hiccups.
  //    * Avoids accessing OpenGL resources soon after their transfer started,
  //      to try to avoid a rendering stall waiting for the transfer to complete
  //      (not sure if that is how OpenGL drivers work, but that is my assumption).
  // 2) And either:
  //    * Decoding is faster than real-time on average.
  //    * It is expected that decoding the remainder of the video finishes
  //      before playback of the frames if starting playback now.
  //    * The video has finished decoding.
  //    * The cache is nearly full with ready frames ("nearly", because it might not get filled completely,
  //      since some frames depend on other frames, thus there might not be exactly enough space for the last frame).
  const int cacheCapacity = GetCacheCapacity();
  
  playbackState.Lock();
  const NextFramesIterator nextFramesIt(&playbackState, &index);
  const s64 currentPlaybackTime = playbackState.GetPlaybackTime();
  const double playbackSpeed = playbackState.GetPlaybackSpeed();
  const PlaybackMode playbackMode = playbackState.GetPlaybackMode();
  const bool forwardPlayback = playbackState.PlayingForward();
  playbackState.Unlock();
  
  int requiredFramesCount, readyFramesCount;
  s64 readyFramesStartTime, readyFramesEndTime;
  int averageDecodingTimeSampleCount;
  s64 averageFrameDecodingTime;
  CheckDecodingProgress(
      nextFramesIt,
      &requiredFramesCount, &readyFramesCount,
      &readyFramesStartTime, &readyFramesEndTime,
      &averageDecodingTimeSampleCount, &averageFrameDecodingTime);
  
  int remainingFramesInVideo;
  if (playbackMode == PlaybackMode::SingleShot) {
    const int currentFrameIndex = index.FindFrameIndexForTimestamp(currentPlaybackTime);
    remainingFramesInVideo = forwardPlayback ? (index.GetFrameCount() - currentFrameIndex) : (currentFrameIndex + 1);
  } else {
    remainingFramesInVideo = numeric_limits<int>::max();
  }
  
  constexpr double realtimeDecodingHeadroomFactor = 0.85;
  
  const s64 averageFrameDuration = (readyFramesCount > 0) ? (fabs(readyFramesEndTime - readyFramesStartTime) / readyFramesCount) : 0;
  const int minimumReadyFramesCount = min(min(5, cacheCapacity), remainingFramesInVideo);
  
  float newBufferingProgress = 0;
  
  if (readyFramesCount >= minimumReadyFramesCount) {
    int remainingFramesToDecodeCount = max(0, remainingFramesInVideo - readyFramesCount);
    if (cacheCapacity >= index.GetFrameCount()) {
      remainingFramesToDecodeCount = min(remainingFramesToDecodeCount, index.GetFrameCount() - readyFramesCount);
    }
    
    const s64 decodingTimeEstimateForRemainderOfVideo = remainingFramesToDecodeCount * averageFrameDecodingTime;
    const double videoRemainderPlaybackTime =
        (forwardPlayback ?
          (index.GetVideoEndTimestamp() - currentPlaybackTime) :
          (currentPlaybackTime - index.GetVideoStartTimestamp())) / playbackSpeed;
    
    // LOG(INFO) << "ShouldBuffer(): averageDecodingTimeSampleCount: " << averageDecodingTimeSampleCount << ", readyFramesCount: " << readyFramesCount << ", averageFrameDecodingTime: " << averageFrameDecodingTime
    //           << ", (realtimeDecodingHeadroomFactor * averageFrameDuration): " << (realtimeDecodingHeadroomFactor * averageFrameDuration);
    // LOG(INFO) << "ShouldBuffer(): remainingFramesToDecodeCount: " << remainingFramesToDecodeCount << ", decodingTimeEstimateForRemainderOfVideo: " << decodingTimeEstimateForRemainderOfVideo
    //           << ", (realtimeDecodingHeadroomFactor * videoRemainderPlaybackTime): " << (realtimeDecodingHeadroomFactor * videoRemainderPlaybackTime)
    //           << ", cacheCapacity: " << cacheCapacity << ", index.GetFrameCount(): " << index.GetFrameCount()
    //           << ", requiredFramesCount: " << requiredFramesCount << ", (cacheCapacity - 2): " << (cacheCapacity - 2);
    
    // DEBUG: Output a "cache health" report while buffering.
    //        This may be useful if you suspect that more frames stay locked than they should.
    // PrintCacheHealth();
    
    if (averageDecodingTimeSampleCount > 0 &&
        readyFramesCount >= 5 &&
        averageFrameDecodingTime <= realtimeDecodingHeadroomFactor * averageFrameDuration) {
      // Decoding is faster than real-time --> start playback.
      if (kVerbose && isBuffering) {
        LOG(INFO) << "Stopping buffering (real-time decoding; readyFramesCount: " << readyFramesCount
                  << ", averageFrameDecodingTime: " << NanosecondsToSeconds(averageFrameDecodingTime)
                  << ", factor * averageFrameDuration: " << NanosecondsToSeconds(realtimeDecodingHeadroomFactor * averageFrameDuration) << ")";
      }
      return false;
    }
    
    // Estimate the buffering progress based on the criteria that will be used below to decide whether to stop buffering
    newBufferingProgress = std::max<double>(newBufferingProgress,
        readyFramesCount / static_cast<double>(readyFramesCount + remainingFramesToDecodeCount));
    const s64 decodingTimeSinceBuffering = readyFramesCount * averageFrameDecodingTime;
    newBufferingProgress = std::max<double>(newBufferingProgress,
        decodingTimeSinceBuffering / static_cast<double>(decodingTimeSinceBuffering + decodingTimeEstimateForRemainderOfVideo - realtimeDecodingHeadroomFactor * videoRemainderPlaybackTime));
    if (cacheCapacity < index.GetFrameCount()) {
      newBufferingProgress = std::max<double>(newBufferingProgress,
          requiredFramesCount / static_cast<double>(cacheCapacity - 2));
    }
    
    if (remainingFramesToDecodeCount == 0 ||
        decodingTimeEstimateForRemainderOfVideo <= realtimeDecodingHeadroomFactor * videoRemainderPlaybackTime ||
        (cacheCapacity < index.GetFrameCount() && requiredFramesCount >= cacheCapacity - 2)) {  // the 2 comes from the two frames that a third frame may depend on
      // We expect to fill the cache before playback of the whole cache will finish if starting playback now, or the cache is nearly full --> start playback.
      // TODO: In this case, we expect to run into issues and require buffering again (unless the video has only a few more frames than the decoded frame cache).
      //       We might want to reduce the playback speed and show a warning to the user to choose a lower video quality, if available.
      if (kVerbose && isBuffering) {
        LOG(INFO) << "Stopping buffering (cache nearly full, or expecting to decode the rest of the video in time for its playback; readyFramesCount: " << readyFramesCount
                  << ", decodingTimeEstimateForRemainderOfVideo: " << NanosecondsToSeconds(decodingTimeEstimateForRemainderOfVideo)
                  << ", factor * videoRemainderPlaybackTime: " << NanosecondsToSeconds(realtimeDecodingHeadroomFactor * videoRemainderPlaybackTime) << ")";
      }
      return false;
    }
  }
  
  // If we decided to keep buffering, check whether we expect that real-time playback is not possible,
  // or we already have been buffering for a certain duration threshold.
  // If so, start showing a buffering indicator.
  if (isBuffering && !showBufferingIndicator) {
    const float bufferingDuration = SecondsDuration(Clock::now() - bufferingStartTime).count();
    
    if (bufferingDuration >= bufferingDurationThresholdInSeconds ||
        (averageDecodingTimeSampleCount > 0 &&
          readyFramesCount >= 2 &&
          averageFrameDecodingTime > realtimeDecodingHeadroomFactor * averageFrameDuration)) {
      showBufferingIndicator = true;
    }
  }
  
  bufferingProgressPercent = std::max(0.f, std::min(100.f, 100 * newBufferingProgress));
  return true;
}

void XRVideo::StartBuffering() {
  isBuffering = true;
  bufferingProgressPercent = 0;
  showBufferingIndicator = false;
  bufferingStartTime = Clock::now();
}

void XRVideo::StopBuffering() {
  isBuffering = false;
  showBufferingIndicator = false;
}

bool XRVideo::SwitchToNextInputStream() {
  // Invalidate all cache items with frames of the old video
  InvalidateAllCacheItems();
  
  // Remove any frames of the old video that may still be queued up for decoding
  ClearLoadingThreadWorkQueues();
  
  // Assign the next input stream to the reader and restart the loading threads
  if (!TakeAndOpenImpl(nextInputStream.release(), nextInputStreamIsStreamingInputStream, nextCacheAllFrames)) {
    return false;
  }
  
  return true;
}

bool XRVideo::TakeAndOpenImpl(InputStream* videoInputStream, bool isStreamingInputStream, bool cacheAllFrames) {
  reader.TakeInputStream(videoInputStream, isStreamingInputStream);
  
  this->cacheAllFrames = cacheAllFrames;
  
  // Clear the metadata, index, and the playback state.
  // They will be initialized asynchronously by the reading thread when it starts up.
  hasMetadata = false;
  index.Clear();
  playbackState.SetPlaybackTimeRange(0, 0);
  playbackState.Seek(0, /*forward*/ true);
  StartBuffering();
  
  // We used to directly resize the decoded frame cache here (and also in Initialize() already) if the cache size is already known (this is if cacheAllFrames == false).
  // However, now we always defer this to when Update() will later notice that async loading has finished.
  // This allows us to know the video's texture size in ResizeDecodedFrameCache(), which is important for some code paths.
  //
  // In detail, the list of reasons is:
  //
  // - External texture initialization happens upon cache resize. This has to happen on the main thread for the Unity plugin,
  //   and we know the video's texture size only after async loading finished.
  // - The OpenGL path must configure its zero-copy texture size after async loading finished (such that the video's texture size is known),
  //   but before any frame starts decoding, which happens as soon as the cache gets allocated.
  //
  // The disadvantage as of now is that with the current design, this introduces a bit of latency until the first frame can start decoding,
  // since after the reading thread signaled that async loading is ready, Update() has to be called to allow the reading thread to continue,
  // and this will be called in a render loop, so loading basically stalls for up to ~16 ms (in case of 60 FPS rendering).
  // TODO: Perhaps this can be mitigated in the future: Can we allow the reading thread to already continue to some extent,
  //       without breaking the conditions mentioned above?
  // TODO: The change also introduced a (very small) inefficiency, since the decoded frame cache now always gets cleared
  //       and reallocated when switching to a new video, whereas it could stay allocated before.
  if (!ResizeDecodedFrameCache(0)) {
    return false;
  }
  SetDecodedFrameCacheInitialized(false);
  
  // Start the loading threads
  if (!StartLoadingThreads()) {
    return false;
  }
  
  return true;
}

}
