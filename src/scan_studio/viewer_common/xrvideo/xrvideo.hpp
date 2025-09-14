#pragma once

#include <vector>

#include <libvis/io/input_stream.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/xrvideo_file.hpp"

#include "scan_studio/viewer_common/timing.hpp"

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/index.hpp"
#include "scan_studio/viewer_common/xrvideo/playback_state.hpp"
#include "scan_studio/viewer_common/xrvideo/reading_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/transfer_thread.hpp"
#include "scan_studio/viewer_common/xrvideo/video_thread.hpp"

namespace scan_studio {
using namespace vis;

struct RenderState;
class XRVideoCommonResources;
class XRVideoRenderLock;
template <class FrameT> class XRVideoRenderLockImpl;

/// This class represents an XRVideo.
/// It contains the individual frames and knows how to render them.
///
/// This is an abstract base class that is independent from the rendering API, allowing for interchangeable use in application code.
/// The derived classes have knowledge about the concrete rendering API:
/// * The XRVideoImpl<...> class derives from XRVideo. Since it knows the rendering-API-specific frame type, it can implement generic algorithms on video frames.
/// * The VulkanXRVideo and OpenGLXRVideo classes then derive from XRVideoImpl<...>, implementing concrete rendering-API-dependent steps.
///
/// To use the class, do:
/// - Construct a VulkanXRVideo, MetalXRVideo, or OpenGLXRVideo.
/// - Call Initialize() to do common initializations (e.g., of the decoded frames cache, uniform buffers, descriptors, ...).
/// - Call TakeAndOpen() to open a file and start the loading threads.
/// - For each frame to be rendered, call Update(elapsedTime) and all rendering functions. Even if playback is paused, call Update(0).
class XRVideo {
 public:
  virtual inline ~XRVideo() {};
  
  /// May optionally be called before Initialize() to set external frame resource callbacks for this XRVideo.
  /// The allocateCallback will always be called when pre-allocating decoded cached frames,
  /// and it may allocate external resources for them. This is used by game engine plugins
  /// if the engine integration requires that resources such as vertex buffers or textures
  /// are allocated with an engine-specific method. The signature of allocateCallback is:
  ///
  ///   bool AllocateExternalFrameResources(int cacheItemIndex, void* frame);
  ///
  /// This callback receives the unique cache item index, and the pointer to the frame instance
  /// to allocate resources for as parameter (e.g., MetalXRVideoFrame or VulkanXRVideoFrame).
  /// The signature of releaseAllCallback is:
  ///
  ///   void ReleaseAllExternalFrameResources();
  ///
  /// Note: These callbacks are used for frame pre-allocation, meaning that they have to allocate
  ///       resources with the maximum possible size; they cannot know the actual size yet.
  ///       This is due to the mostly single-threaded design of Unity, where we only want the
  ///       callbacks to be called on the main thread, not in the frame decoding thread, which
  ///       would know the actual required resource sizes.
  void SetExternalFrameResourcesCallbacks(std::function<bool(int, void*)> allocateCallback, std::function<void()> releaseAllCallback);
  
  bool Initialize(int cachedDecodedFrameCount, bool verboseDecoding, XRVideoCommonResources* commonResources);
  virtual void Destroy() = 0;
  
  /// Opens the XRVideo from the given input stream, taking ownership of the input stream.
  /// Note that the input stream remains open while the XRVideo is used since frames are loaded during playback.
  /// Returns true on success, false on failure.
  ///
  /// It is possible to call TakeAndOpen() multiple times. Starting from the second time, the XRVideo will
  /// switch the active video to the new video. In these cases, the switch does not happen immediately,
  /// since it is assumed that this happens while the application runs, and an immediate switch would cause stuttering.
  /// Instead, TakeAndOpen() then only initiates the switch (requesting the loading threads to exit),
  /// and the switch will actually be finalized during one of the following calls to Update() (once the
  /// loading threads actually exited). SwitchedToMostRecentVideo() can be used to query whether the switch
  /// was done.
  ///
  /// `isStreamingInputStream` is used to avoid the need for dynamic_cast
  /// or having an RTTI mechanism built into libvis' `InputStream`.
  /// It must be set to true if a StreamingInputStream is passed in, false otherwise.
  /// This is used to improve streaming performance by calling additional functions
  /// on StreamingInputStream to pre-read data.
  ///
  /// TODO: Since this takes a raw pointer, code using this function tends to be dangerous,
  ///       potentially forgetting to delete the pointer in error cases or deleting it when it was already taken.
  ///       Use a unique or shared pointer instead.
  bool TakeAndOpen(InputStream* videoInputStream, bool isStreamingInputStream, bool cacheAllFrames);
  
  /// Updates the XRVideo's playback state by the elapsed time.
  /// Returns the updated playback time.
  s64 Update(s64 elapsedNanoseconds);
  
  /// Returns a "render lock" for the current state of the XRVideo.
  /// This lock object encapsulates (at creation time):
  /// * The current playback time within the current frame
  /// * Read-locks on the frames that are necessary to render the video at this time.
  ///
  /// This design is intended for multi-threaded rendering, where the rendering is done in a different thread than the
  /// call to Update(): After the Update() call, a render lock may be created and passed on to the render
  /// thread to perform the rendering asynchronously, even if the update thread will interact with the video
  /// again before rendering finishes.
  ///
  /// Attention: If no frames for display have been decoded yet, then CreateRenderLock() returns nullptr.
  virtual unique_ptr<XRVideoRenderLock> CreateRenderLock() = 0;
  
  /// Seeks to the given time, with the given playback direction (forward or backward).
  ///
  /// Calling Seek() here on the XRVideo instead of directly on its PlaybackState performs additional
  /// higher-level maintenance to make the seeking operation behave better: Frames in the loading thread
  /// queues will be cleared, the XRVideo will start buffering if it seems appropriate, and if the video
  /// is being streamed, then pre-scheduled streaming ranges will be canceled.
  void Seek(s64 timestamp, bool forward);
  
  /// Returns whether the current frame, as determined by the video's playback state,
  /// is ready for display. For example, this function may be used after calling Seek()
  /// to poll for when the seeked-to frame is ready for display.
  ///
  /// Note that if the current playback time is out of bounds of the video time range, then
  /// this function always returns true (to prevent possible hang-ups of code waiting for
  /// this to become true).
  bool IsCurrentFrameDisplayReady();
  
  
  // --- Accessors ---
  
  /// Returns the state of asynchronously loading the video (loading, error, ready).
  inline XRVideoAsyncLoadState GetAsyncLoadState() const { return asyncLoadState; }
  
  /// Returns whether Metadata() gives valid data.
  /// Important: Must only be called after GetAsyncLoadState() has returned XRVideoAsyncLoadState::Ready.
  inline bool HasMetadata() const {
    if (asyncLoadState != XRVideoAsyncLoadState::Ready) { LOG(ERROR) << "This attribute must only be accessed after async loading finished successfully"; }
    return hasMetadata;
  }
  
  /// Returns the XRVideo metadata.
  /// Important: Must only be called after GetAsyncLoadState() has returned XRVideoAsyncLoadState::Ready.
  inline const XRVideoMetadata& GetMetadata() const {
    if (asyncLoadState != XRVideoAsyncLoadState::Ready) { LOG(ERROR) << "This attribute must only be accessed after async loading finished successfully"; }
    return metadata;
  }
  
  /// Returns the XRVideo's texture size.
  /// Important: Must only be called after GetAsyncLoadState() has returned XRVideoAsyncLoadState::Ready.
  inline u16 TextureWidth() const {
    if (asyncLoadState != XRVideoAsyncLoadState::Ready) { LOG(ERROR) << "This attribute must only be accessed after async loading finished successfully"; }
    return textureWidth;
  }
  inline u16 TextureHeight() const {
    if (asyncLoadState != XRVideoAsyncLoadState::Ready) { LOG(ERROR) << "This attribute must only be accessed after async loading finished successfully"; }
    return textureHeight;
  }
  
  /// Returns the playback state.
  /// Important: Getting and setting the playback position as well as getting the start and end timestamp must only be done after GetAsyncLoadState() has returned XRVideoAsyncLoadState::Ready.
  inline PlaybackState& GetPlaybackState() { return playbackState; }
  inline const PlaybackState& GetPlaybackState() const { return playbackState; }
  
  /// Returns the frame index.
  /// Important: Must only be called after GetAsyncLoadState() has returned XRVideoAsyncLoadState::Ready.
  inline const FrameIndex& Index() const {
    if (asyncLoadState != XRVideoAsyncLoadState::Ready) { LOG(ERROR) << "This attribute must only be accessed after async loading finished successfully"; }
    return index;
  }
  
  /// Returns whether the video is in the buffering state.
  inline bool IsBuffering() const { return isBuffering; }
  
  /// Returns whether a buffering indicator should be shown to the user.
  inline bool BufferingIndicatorShouldBeShown() const { return isBuffering && showBufferingIndicator; }
  
  /// Returns the approximate buffering progress. This may be displayed as buffering progress indication to the user.
  inline float GetBufferingProgressPercent() const { return bufferingProgressPercent; }
  
  /// Returns whether after the second or following call to TakeAndOpen(), the switch to the new
  /// video has been carried out (in a subsequent call to Update()) and a frame from the new video can be displayed.
  /// See the comment on TakeAndOpen() for details.
  inline bool SwitchedToMostRecentVideo() const { return nextInputStream == nullptr && asyncLoadState == XRVideoAsyncLoadState::Ready && HaveValidFramesForRendering(); }
  
  /// Returns the XRVideoCommonResources object that this XRVideo uses.
  inline const XRVideoCommonResources* CommonResources() const { return commonResources; }
  inline XRVideoCommonResources* CommonResources() { return commonResources; }
  
 protected:
  virtual bool InitializeImpl() = 0;
  
  virtual bool ResizeDecodedFrameCache(int cachedDecodedFrameCount) = 0;
  virtual void SetDecodedFrameCacheInitialized(bool initialized) = 0;
  
  /// This is called after a file is opened by TakeAndOpen() to start the frame loading threads.
  virtual bool StartLoadingThreads() = 0;
  virtual void RequestLoadingThreadsToExit() = 0;
  virtual bool AreLoadingThreadsExited() const = 0;
  
  /// This is always called with frameIndices in the order:
  /// currentFrame, baseKeyframe, predecessor,
  /// with only those frame(s) present that are required for display.
  /// Note that baseKeyframe and predecessor might be the same frame. In this case, only one
  /// entry for both is included.
  virtual bool LockFramesForRendering(const vector<int>& frameIndices) = 0;
  
  /// Returns true if this XRVideo has frames locked for rendering that are from the current video.
  /// Note that the XRVideo may render a frame even if this returns false: This happens if the XRVideo's
  /// video has been switched, but it still holds on to locked frames of the previous video.
  virtual bool HaveValidFramesForRendering() const = 0;
  
  virtual int GetCacheCapacity() const = 0;
  
  virtual void InvalidateAllCacheItems() = 0;
  
  virtual void CheckDecodingProgress(
      const NextFramesIterator& nextPlayedFramesIt,
      int* requiredFramesCount, int* readyFramesCount,
      s64* readyFramesStartTime, s64* readyFramesEndTime,
      int* averageFrameDecodingTimeSampleCount, s64* averageFrameDecodingTime) = 0;
  
  virtual void DebugPrintCacheHealth() {}
  
  virtual void ClearLoadingThreadWorkQueues() = 0;
  
  bool ShouldBuffer();
  
  void StartBuffering();
  void StopBuffering();
  
  bool SwitchToNextInputStream();
  
  bool TakeAndOpenImpl(InputStream* videoInputStream, bool isStreamingInputStream, bool cacheAllFrames);
  
  
  // --- Asynchronously initialized metadata / state (at reading thread startup) ---
  //
  // These members may only be accessed here if asyncLoadState is XRVideoAsyncLoadState::Ready.
  // If asyncLoadState is XRVideoAsyncLoadState::Loading, then the reading thread has exclusive (write) access to them.
  
  /// Whether `metadata` contains valid data.
  bool hasMetadata = false;
  
  /// XRVideo metadata. Only valid if `hasMetadata` is true.
  XRVideoMetadata metadata;
  
  /// The video's texture size. (Once we update the XRV file format, this should get integrated into XRVideoMetadata).
  u16 textureWidth;
  u16 textureHeight;
  
  /// An index giving information about the frames in the XRVideo.
  FrameIndex index;
  
  /// The current playback state of the video.
  PlaybackState playbackState;
  
  // --- End of asynchronously initialized metadata / state ---
  
  
  /// Whether an asynchronous load of the video metadata / state is in progress, has finished, or failed.
  /// Controls access to the members above.
  atomic<XRVideoAsyncLoadState> asyncLoadState = XRVideoAsyncLoadState::Error;
  
  /// The amount by which the current frame has passed:
  /// 0 means that the current frame is shown as-is.
  /// (almost) 1 means that the current frame is shown (almost) deformed to the next frame.
  float currentIntraFrameTime;
  
  /// Whether playback is currently paused to wait for frame decoding to do its work.
  bool isBuffering = true;
  
  /// The time at which we started buffering.
  TimePoint bufferingStartTime;
  
  /// Whether an indicator should be shown that the video is buffering.
  /// This is separate from `isBuffering` since we only want to show an indicator if we expect
  /// that buffering will take a significant amount of time.
  bool showBufferingIndicator = false;
  
  /// Cached buffering progress (in percent)
  float bufferingProgressPercent = 0;
  
  /// Whether to output verbose log messages for frame decoding.
  bool verboseDecoding;
  
  /// The cached decoded frame count configured with Initialize().
  int cachedDecodedFrameCount;
  
  /// Whether to cache all frames of the video.
  bool cacheAllFrames;
  
  /// The XRVideo reader (which remains open during playback).
  /// Outside of TakeAndOpenImpl() and except for reader.IsOpen(),
  /// this is exclusively used by the reading thread (and thus other uses would not be thread-safe).
  XRVideoReader reader;
  
  /// The next input stream to switch to.
  /// This is used on multiple calls to TakeAndOpen().
  unique_ptr<InputStream> nextInputStream;
  bool nextInputStreamIsStreamingInputStream;
  bool nextCacheAllFrames;
  
  /// External frame resources callbacks (may be null)
  std::function<bool(int, void*)> allocateExternalFrameResourcesCallback;
  std::function<void()> releaseAllExternalFrameResourcesCallback;
  
  /// Common resources (cast to the derived class, e.g., VulkanXRVideoCommonResources, to use)
  XRVideoCommonResources* commonResources;
};


/// Middle class in the XRVideo inheritance chain.
/// Implements generic functions that operate on frames.
/// See the documentation comment on the XRVideo class.
/// FrameT will be VulkanXRVideoFrame or OpenGLXRVideoFrame.
template <class FrameT>
class XRVideoImpl : public XRVideo {
 public:
  virtual inline ~XRVideoImpl() {}
  
 protected:
  virtual void SetDecodedFrameCacheInitialized(bool initialized) override {
    readingThread.SetDecodedFrameCacheInitialized(initialized);
  }
  
  /// Attention: OpenGLXRVideo overrides this function in order to do some SDL-specific thread-safety handling.
  virtual bool StartLoadingThreads() override {
    transferThread.StartThread(verboseDecoding);
    decodingThread.StartThread(verboseDecoding, &transferThread);
    videoThread.StartThread(verboseDecoding, &decodingThread, &index);
    readingThread.StartThread(verboseDecoding, &playbackState, &videoThread, &decodingThread, &decodedFrameCache, &asyncLoadState, &hasMetadata, &metadata, &textureWidth, &textureHeight, &index, &reader);
    return true;
  }
  
  virtual void RequestLoadingThreadsToExit() override {
    readingThread.RequestThreadToExit();
    videoThread.RequestThreadToExit();
    decodingThread.RequestThreadToExit();
    transferThread.RequestThreadToExit();
  }
  
  virtual bool AreLoadingThreadsExited() const override {
    return !readingThread.IsThreadRunning() &&
           !videoThread.IsThreadRunning() &&
           !decodingThread.IsThreadRunning() &&
           !transferThread.IsThreadRunning();
  }
  
  virtual bool LockFramesForRendering(const vector<int>& frameIndices) override {
    auto newLockedFrames = decodedFrameCache.LockFramesForReading(frameIndices);
    
    // On failure, keep the old locks in framesLockedForRendering so we can continue to show the old frame.
    const bool success = !newLockedFrames.empty();
    if (success) {
      framesLockedForRendering = move(newLockedFrames);
    }
    
    return success;
  }
  
  virtual bool HaveValidFramesForRendering() const override {
    return !framesLockedForRendering.empty() && framesLockedForRendering.front().GetFrameIndex() >= 0;
  }
  
  virtual int GetCacheCapacity() const override {
    return decodedFrameCache.GetCapacity();
  }
  
  virtual void InvalidateAllCacheItems() override {
    decodedFrameCache.InvalidateAllCacheItems();
  }
  
  virtual void CheckDecodingProgress(
      const NextFramesIterator& nextPlayedFramesIt,
      int* requiredFramesCount, int* readyFramesCount,
      s64* readyFramesStartTime, s64* readyFramesEndTime,
      int* averageFrameDecodingTimeSampleCount, s64* averageFrameDecodingTime) override {
    decodedFrameCache.CheckDecodingProgress(nextPlayedFramesIt, requiredFramesCount, readyFramesCount, readyFramesStartTime, readyFramesEndTime);
    transferThread.GetAverageDecodingTime(averageFrameDecodingTimeSampleCount, averageFrameDecodingTime);
  }
  
  virtual void DebugPrintCacheHealth() override {
    decodedFrameCache.DebugPrintCacheHealth();
  }
  
  virtual void ClearLoadingThreadWorkQueues() override {
    // In case the reading thread is in its loop to read a range of frames, abort it.
    // If ClearLoadingThreadWorkQueues() is called for seeking, we hold the playbackState lock,
    // which also guarantees that the reading thread will not read or queue up any further frames
    // after AbortCurrentFrames() returns.
    readingThread.AbortCurrentFrames();
    
    // Clear the video thread's work queue and also make sure that it will not queue up
    // any further frames after the function call below returns.
    videoThread.ClearQueueAndAbortCurrentFrames();
    
    // Clear the decoding thread's work queues.
    // For this thread, it does not matter whether it will still queue up a last frame afterwards,
    // as the transfer thread will handle it fine and if the frame won't be needed after the seek,
    // then it will simply be removed from the decoded frames cache again.
    decodingThread.ClearQueues();
    
    // Old reasoning:
    // ----
    // We don't touch the transfer thread here since it does not start any actions,
    // it only waits for transfers that have already been started and cannot be aborted anymore.
    //
    // In addition, consider that clearing the transfer thread's queue here would most likely cause bugs
    // with many of the XRVideo frame implementations because they most likely do not account
    // for the possibility that a frame may be re-initialized directly after Initialize() is called,
    // without WaitForResourceTransfers() being called first to wait for the first Initialize()'s transfers!
    // ----
    //
    // This caused bugs, because work items could remain in the transfer thread queue when switching to
    // a new video, which pointed to invalid cached frames after clearing the decoded frame cache.
    // Once the transfer thread restarted, this caused random issues.
    //
    // Thus, this has been changed to clear the transfer thread's work queue as well.
    // Any bugs appearing in XRVideo frame implementations due to this should get fixed.
    transferThread.ClearQueue(/*finishAllTransfers*/ false);
  }
  
  /// XRVideo frames
  DecodedFrameCache<FrameT> decodedFrameCache;
  vector<ReadLockedCachedFrame<FrameT>> framesLockedForRendering;
  
  // Frame loading threads
  ReadingThread<FrameT> readingThread;
  VideoThread<FrameT> videoThread;
  DecodingThread<FrameT> decodingThread;
  TransferThread<FrameT> transferThread;
};


/// Represents a state of an XRVideo that has been locked for rendering.
/// This is intended for multi-threaded rendering, where an "update thread" updates the video playback time,
/// then creates a render lock which is passed on to a "render thread" that uses it at some later point to render the video at that time.
///
/// Note that this lock does *not* mean exclusive access to the XRVideo; the implementation supports
/// a render thread that sequentially (!) consumes a queue of locks that have been created, but not, for example,
/// attempted concurrent operation on multiple locks. In other words, the render thread must operate on the
/// received locks in order and must finish operating on a lock before proceeding to the next one.
class XRVideoRenderLock {
 public:
  virtual inline ~XRVideoRenderLock() {}
  
  // --- Render functions ---
  // First, PrepareFrame() must be called, then PrepareView() and RenderView() must be called for each view
  // (e.g., one view for flatscreen display, or two views for VR display).
  // Each PrepareView() must be directly followed by a RenderView(), i.e.,
  // the sequence (PrepareView, PrepareView, RenderView, RenderView) is *not* supported.
  
  /// Call once at the start of a frame.
  /// Prepares to render a frame at the given floating-point frame index.
  virtual void PrepareFrame(RenderState* renderState) = 0;
  
  /// Call before each view's render pass.
  virtual void PrepareView(int viewIndex, bool flipBackFaceCulling, bool useSurfaceNormalShading, RenderState* renderState) = 0;
  
  /// Call during each view's render pass.
  virtual void RenderView(RenderState* renderState) = 0;
  
  /// If this returns true, SetModelViewProjection() should be called as late as possible before
  /// submitting the command buffer (i.e., after RenderView()), with the most up-to-date matrix at this point in time.
  /// If this returns false, SetModelViewProjection() must be called before RenderView().
  virtual bool SupportsLateModelViewProjectionSetting() = 0;
  
  /// Sets the model-view-projection matrix.
  /// Must be called either before or should be called after RenderView(),
  /// depending on what is returned by SupportsLateModelViewProjectionSetting().
  /// viewIndex matches that given to PrepareView(): There is one viewIndex per call to PrepareView() and RenderView().
  /// On the other hand, multiViewIndex may be used for single-pass multi-view (e.g., VR) rendering, where one call to RenderView() renders to multiple render targets.
  virtual void SetModelViewProjection(int viewIndex, int multiViewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) = 0;
  
  
  // --- Accessors ---
  
  inline const XRVideo* Video() const { return video; }
  inline XRVideo* Video() { return video; }
  
  // Accessors used for the Unity plugin (that uses external resources):
  virtual inline int GetDeformationStateResourceIndex() const { LOG(ERROR) << "TODO: Implement this function for the used render API"; return -1; };
  virtual int GetDisplayFrameCacheItemIndex() const = 0;
  virtual int GetKeyframeCacheItemIndex() const = 0;
  virtual const XRVideoFrameMetadata& GetKeyframeMetadata() const = 0;
  
  inline float CurrentIntraFrameTime() const { return currentIntraFrameTime; }
  
  inline void SetUseSurfaceNormalShading(bool enable) { useSurfaceNormalShading = enable; }
  inline bool UseSurfaceNormalShading() const { return useSurfaceNormalShading; }
  
  /// For internal use
  template <class FrameT> const XRVideoRenderLockImpl<FrameT>& GetImpl() const { return *reinterpret_cast<XRVideoRenderLockImpl<FrameT>*>(this); }
  template <class FrameT> XRVideoRenderLockImpl<FrameT>& GetImpl() { return *reinterpret_cast<XRVideoRenderLockImpl<FrameT>*>(this); }
  
 protected:
  inline XRVideoRenderLock(XRVideo* video, float currentIntraFrameTime)
      : currentIntraFrameTime(currentIntraFrameTime),
        video(video) {}
  
  /// May be assigned in XRVideo::RenderView().
  bool flipBackFaceCulling;
  bool useSurfaceNormalShading;
  
  /// The current intra-frame time, obtained from the XRVideo on creation
  float currentIntraFrameTime;
  
  XRVideo* video;
};


template <class FrameT>
class XRVideoRenderLockImpl : public XRVideoRenderLock {
 public:
  inline XRVideoRenderLockImpl(
      XRVideoImpl<FrameT>* video,
      float currentIntraFrameTime,
      vector<ReadLockedCachedFrame<FrameT>>&& framesLockedForRendering)
      : XRVideoRenderLock(video, currentIntraFrameTime),
        framesLockedForRendering(move(framesLockedForRendering)) {}
  
  virtual inline ~XRVideoRenderLockImpl() {}
  
  /// Returns the currently displayed frame from among the locked frames.
  inline const ReadLockedCachedFrame<FrameT>& GetDisplayFrame() const {
    return framesLockedForRendering[0];
  }
  
  /// Returns the previous frame from among the locked frames if it is present,
  /// returns nullptr if the previous frame is not present.
  inline const ReadLockedCachedFrame<FrameT>* GetPreviousFrame() const {
    return (framesLockedForRendering.size() == 1) ? nullptr : &framesLockedForRendering.back();
  }
  
  /// Returns the base keyframe from among the locked frames
  /// (this might be equal to GetDisplayFrame() if the current frame is a keyframe).
  inline const ReadLockedCachedFrame<FrameT>& GetKeyframe() const {
    return (framesLockedForRendering.size() == 1) ? framesLockedForRendering[0] : framesLockedForRendering[1];
  }
  
  virtual inline int GetDisplayFrameCacheItemIndex() const override {
    return GetDisplayFrame().GetCacheItemIndex();
  }
  
  virtual inline int GetKeyframeCacheItemIndex() const override {
    return GetKeyframe().GetCacheItemIndex();
  }
  
  virtual const XRVideoFrameMetadata& GetKeyframeMetadata() const override {
    return GetKeyframe().GetFrame()->GetMetadata();
  }
  
  inline const vector<ReadLockedCachedFrame<FrameT>>& FramesLockedForRendering() const {
    return framesLockedForRendering;
  }
  
 protected:
  vector<ReadLockedCachedFrame<FrameT>> framesLockedForRendering;
};

}
