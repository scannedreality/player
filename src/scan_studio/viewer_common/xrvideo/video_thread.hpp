#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#ifdef __EMSCRIPTEN__
  #include <emscripten/threading.h>
#endif

#include <dav1d/dav1d.h>
#include <../vcs_version.h>

#include <zstd.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/util.hpp"

#include "scan_studio/viewer_common/xrvideo/decoding_thread.hpp"

namespace scan_studio {
using namespace vis;

// Copy of dav1d_num_logical_processors() (which is private in dav1d),
// with the logging removed since we don't need that.
int dav1d_num_logical_processors_copy();

/// Base class for classes implementing zero-copy texture decoding with dav1d.
class Dav1dZeroCopy {
 public:
  virtual inline ~Dav1dZeroCopy() {}
  
  inline void Configure(u32 videoWidth, u32 videoHeight) {
    dav1dZeroCopyVideoWidth = videoWidth;
    dav1dZeroCopyVideoHeight = videoHeight;
  }
  
  /// Must allocate the given picture, or ideally, take it from a memory pool.
  ///
  /// For documentation on which members of the passed-in Dav1dPicture object
  /// this function must initialize, and alignment and padding requirements of
  /// the allocated memory, see dav1d's documentation of its `struct Dav1dPicAllocator`.
  ///
  /// This function is called on the thread that calls dav1d_send_data(), dav1d_get_picture().
  virtual int Dav1dAllocPictureCallback(Dav1dPicture* pic) = 0;
  
  /// Must release the given picture, or ideally, return it to a memory pool.
  ///
  /// This function may be called from different threads and must thus be thread-safe.
  virtual void Dav1dReleasePictureCallback(Dav1dPicture* pic) = 0;
  
 protected:
  u32 dav1dZeroCopyVideoWidth = 0;
  u32 dav1dZeroCopyVideoHeight = 0;
};

/// Thread which controls the AV.1 video texture decoding using dav1d.
template <typename FrameT>
class VideoThread {
 public:
  ~VideoThread() {
    Destroy();
  }
  
  void Destroy() {
    WaitForThreadToExit();
    
    ClearQueueAndAbortCurrentFrames();
  }
  
  /// Configures the thread to use zero-copy for dav1d, using the callbacks of the passed-in object.
  /// The object needs to remain valid during the lifetime of the VideoThread.
  void SetUseDav1dZeroCopy(Dav1dZeroCopy* dav1dZeroCopy) {
    this->dav1dZeroCopy = dav1dZeroCopy;
  }
  
  void StartThread(bool verboseDecoding, DecodingThread<FrameT>* decodingThread, FrameIndex* frameIndex) {
    if (thread.joinable()) { thread.join(); }
    
    this->verboseDecoding = verboseDecoding;
    this->decodingThread = decodingThread;
    this->frameIndex = frameIndex;
    
    threadRunning = true;
    quitRequested = false;
    thread = std::thread(std::bind(&VideoThread::ThreadMain, this));
  }
  
  void RequestThreadToExit() {
    workQueueMutex.lock();
    quitRequested = true;
    workQueueMutex.unlock();
    newWorkCondition.notify_all();
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
  
  bool QueueFrame(int frameIndex, const shared_ptr<XRVideoFrameMetadata>& frameMetadata, const shared_ptr<vector<u8>>& frameData, const u8* frameContentPtr) {
    unique_lock<mutex> lock(workQueueMutex);
    
    if (!frameMetadata->isKeyframe && frameIndex != lastFrameIndexQueuedForDecoding + 1) {
      if (verboseDecoding) {
        LOG(WARNING) << "VideoThread: Failed to queue a frame, isKeyframe: " << frameMetadata->isKeyframe
                     << ", frameIndex: " << frameIndex << ", lastFrameIndexQueuedForDecoding: " << lastFrameIndexQueuedForDecoding;
      }
      return false;
    }
    
    WorkItem* newItem = new WorkItem();
    newItem->frameIndex = frameIndex;
    newItem->frameMetadata = frameMetadata;
    newItem->frameData = frameData;
    newItem->frameContentPtr = frameContentPtr;
    newItem->lastFrameIndexQueuedForDecoding = lastFrameIndexQueuedForDecoding;
    workQueue.push_back(newItem);
    
    lastFrameIndexQueuedForDecoding = frameIndex;
    
    lock.unlock();
    newWorkCondition.notify_one();
    
    return true;
  }
  
  void ClearQueueAndAbortCurrentFrames() {
    // NOTE: Never lock the two mutexes below in the opposite order, or there will be the chance of a deadlock.
    workQueueMutex.lock();
    abortMutex.lock();
    abortCurrentFrames = true;
    abortMutex.unlock();
    
    // After calling dav1d_flush() (which will be done after we set abortCurrentFrames = true above),
    // we have to pass a keyframe to dav1d. So we cannot continue after the last decoded frame, as this would do:
    // if (!workQueue.empty()) {
    //   lastFrameIndexQueuedForDecoding = workQueue.front()->lastFrameIndexQueuedForDecoding;
    // }
    lastFrameIndexQueuedForDecoding = -1;
    
    for (WorkItem* item : workQueue) {
      delete item;
    }
    workQueue.clear();
    
    workQueueMutex.unlock();
  }
  
  inline int GetLastFrameIndexQueuedForDecoding() {
    workQueueMutex.lock();
    const int result = lastFrameIndexQueuedForDecoding;
    workQueueMutex.unlock();
    return result;
  }
  
 private:
  struct FrameBeingDecoded;
  
  struct WorkItem {
    /// Index of the frame.
    int frameIndex;
    
    /// Metadata of the frame to decode.
    shared_ptr<XRVideoFrameMetadata> frameMetadata;
    
    /// The compressed frame data to decode.
    shared_ptr<vector<u8>> frameData;
    
    /// Pointer to the start of the frame's encoded content (within the frameData buffer).
    const u8* frameContentPtr;
    
    /// The last frame index queued for decoding before this frame.
    /// This is used in case we later remove this frame from the queue again:
    /// Then, we know that after decoding all previous queue items, the decoding state
    /// will be at this frame index.
    int lastFrameIndexQueuedForDecoding;
  };
  
  void ThreadMain() {
    const bool initializedSuccessfully = InitializeWorkerThread();
    if (!initializedSuccessfully) {
      threadRunning = false;
      return;
    }
    
    while (!quitRequested) {
      unique_lock<mutex> lock(workQueueMutex);
      
      while (workQueue.empty() && !quitRequested) {
        newWorkCondition.wait(lock);
      }
      if (quitRequested) {
        break;
      }
      
      WorkItem* item = workQueue.front();
      workQueue.erase(workQueue.begin());
      
      // If the last frames were aborted, make sure that we won't get any further frames that dav1d had cached internally.
      if (abortCurrentFrames) {
        dav1d_flush(dav1dCtx.get());
        frameQueue.clear();
      }
      
      abortCurrentFrames = false;
      
      lock.unlock();
      
      ProcessItem(item);
      delete item;
      
      // If, after processing a frame, our work queue is empty, drain any remaining frames from dav1d before waiting for new work.
      // This is necessary to avoid stalling decoding in at least two cases:
      // - If decoding is slow, playback waits for the cache to be filled.
      //   To be able to fill the cache, we must retrieve the textures for all in-progress frames;
      //   the reading thread will not continue reading beyond the size of the cache.
      // - If a single frame in front of the playback position is missing, but the cache is full otherwise, we have to be able
      //   to decode that single frame individually, without caching up more frames.
      //   This case would even stall decoding if we did not wait for the cache to become full in the situation of the first case.
      //
      // Note that seemingly, if we start draining the delayed frames from dav1d,
      // then we have to keep going until we got all of them before we pass in new work
      // (or abort and call dav1d_flush(), after which a keyframe must be passed in next).
      // Otherwise, after a while, dav1d_get_picture() will hang.
      workQueueMutex.lock();
      const bool workQueueIsEmpty = workQueue.empty();
      workQueueMutex.unlock();
      
      if (workQueueIsEmpty) {
        while (!quitRequested && !abortCurrentFrames) {
          bool pictureReceived = false;
          if (!GetPictures(/*atEndOfVideo*/ false, &pictureReceived)) {
            break;
          }
          
          if (pictureReceived && verboseDecoding) {
            LOG(WARNING) << "Received picture when work queue was empty";
          } else if (!pictureReceived) {
            break;
          }
        }
      }
    }
    
    DeinitializeWorkerThread();
    threadRunning = false;
  }
  
  bool InitializeWorkerThread() {
    SCAN_STUDIO_SET_THREAD_NAME("scan-video");
    
    // Initialize dav1d context
    const char* version = dav1d_version();
    if (strcmp(version, DAV1D_VERSION)) {
      LOG(WARNING) << "Dav1d version mismatch (retrieved from library: " << version << ", compiled into executable: " << DAV1D_VERSION << ")";
      return false;
    }
    
    Dav1dSettings dav1dSettings;
    dav1d_default_settings(&dav1dSettings);
    
    // We currently encode the videos with four tiles,
    // so we should use at least four threads for good parallelism even within a single frame.
    #ifdef __EMSCRIPTEN__
      // For emscripten, a fixed number of threads is pre-allocated in a pool (configured in the CMake file with `PTHREAD_POOL_SIZE`).
      // There may also be browser limitations on the number of threads that we can create.
      // Thus, we choose a thread count between 4 and 8 in this case.
      // Note that emscripten_num_logical_cores() returns the value of Javascript's `navigator.hardwareConcurrency`.
      //
      // NOTE: Testing on a 9th generation iPad which supposedly has 6 cores,
      //       emscripten_num_logical_cores() returned 4,
      //       and the rendering performance degraded (very heavy stuttering) when manually setting dav1dSettings.n_threads to 6.
      //       It also seemed that the introduction of the video thread (I guess, not sure) already degraded rendering performance there (noticeably more stuttering than before),
      //       even though that thread itself hardly does anything else than waiting.
      //       Because of that, for now on iOS the thread count is limited to 4 to prevent catastrophic stuttering.
      const int isIOS = MAIN_THREAD_EM_ASM_INT(({
        var isIOS = /(iPad|iPhone|iPod)/g.test(navigator.userAgent);
        return isIOS ? 1 : 0;
      }));
      
      if (isIOS) {
        dav1dSettings.n_threads = 4;
      } else {
        dav1dSettings.n_threads = std::max(4, std::min(8, emscripten_num_logical_cores()));
      }
    #else
      // For other platforms, we use the thread count that dav1d would use as default,
      // while ensuring to use at least four in cases where it cannot be determined
      // (for the four tiles in our video encoding).
      // Unfortunately, in order to do this, we have to use a copy of dav1d's thread count selection
      // function dav1d_num_logical_processors() because that function is private in dav1d,
      // and dav1d_default_settings() initializes dav1dSettings.n_threads to zero instead
      // of to the result of dav1d_num_logical_processors().
      //
      // NOTE: This was changed back to the default of constant 4 because on the Quest 2 (with the Unreal Engine plugin),
      //       the example app ran very significantly smoother with 4 threads than with 8 (the latter given by dav1d_num_logical_processors_copy()).
      dav1dSettings.n_threads = 4;  // std::max(4, dav1d_num_logical_processors_copy());
    #endif
    
    // It can be very important for decoding bandwidth to have max_frame_delay > 1.
    // We use dav1d's default. For n_threads == 4, that would be 2.
    dav1dSettings.max_frame_delay = 0;
    
    // Don't apply any grain to get better performance in case grain is present.
    dav1dSettings.apply_grain = 0;
    
    dav1dSettings.logger.cookie = nullptr;
    dav1dSettings.logger.callback = &Dav1dLogCallback;
    
    if (dav1dZeroCopy) {
      dav1dSettings.allocator.cookie = this;
      dav1dSettings.allocator.alloc_picture_callback = &Dav1dAllocPictureCallback;
      dav1dSettings.allocator.release_picture_callback = &Dav1dReleasePictureCallback;
    }
    
    Dav1dContext* dav1dCtxUnsafe = nullptr;
    int res = dav1d_open(&dav1dCtxUnsafe, &dav1dSettings);
    if (res != 0) {
      LOG(ERROR) << "dav1d_open() returned " << res;
      return false;
    }
    dav1dCtx.reset(dav1dCtxUnsafe, [](Dav1dContext* ctx) { dav1d_close(&ctx); });
    
    if (verboseDecoding) {
      LOG(INFO) << "Dav1d configuration: n_threads = " << dav1dSettings.n_threads << ", max_frame_delay: "
                << dav1dSettings.max_frame_delay << ", actual frame delay: " << dav1d_get_frame_delay(&dav1dSettings);
    }
    
    return true;
  }
  
  void DeinitializeWorkerThread() {
    dav1dCtx.reset();
    zstdCtx.reset();
  }
  
  void ProcessItem(WorkItem* item) {
    // NOTE: The control flow in this function matches the example given in the documentation comment
    //       on the dav1d_get_picture() function. I think that following this scheme (and running it in a separate thread)
    //       is important to get the best decoding parallelism, since dav1d_send_data() always seems to block
    //       until the first decoded frame is available, dav1d_get_picture() may also block, and both functions may
    //       require calling the other to progress.
    
    const auto& frameMetadata = *item->frameMetadata;
    const u8* textureDataPtr = item->frameContentPtr + frameMetadata.compressedMeshSize + frameMetadata.compressedDeformationStateSize;
    
    // Special case: If frameMetadata.compressedRGBSize is zero, then no texture is stored because
    //               the video frame is empty.
    if (frameMetadata.compressedRGBSize == 0) {
      if (frameQueue.empty()) {
        if (!OutputEmptyPicture(item->frameIndex)) { return; }
      } else {
        frameQueue.emplace_back(item->frameIndex, /*isEmpty*/ true, frameMetadata.textureWidth, frameMetadata.textureHeight);
      }
      return;
    }
    
    // Special case: If zstd compression is used, decompress with zstd.
    // TODO: I presume that for these, it would be beneficial to decode multiple frames at once for best performance on multi-core CPUs.
    if (frameMetadata.zstdRGBTexture) {
      ProcessZStdTexture(item, textureDataPtr);
      return;
    }
    
    shared_ptr<vector<u8>>* frameDataPointerCopy = new shared_ptr<vector<u8>>(item->frameData);
    Dav1dData data = {0};
    
    int res = dav1d_data_wrap(&data, textureDataPtr, frameMetadata.compressedRGBSize, &Dav1dFreeDataCallback, /*cookie*/ frameDataPointerCopy);
    if (res != 0) {
      LOG(ERROR) << "dav1d_data_wrap() returned " << res;
      delete frameDataPointerCopy;
      return;
    }
    
    // As long as there is data to consume ...
    do {
      // Try sending the next data packet to dav1d.
      // Keep going even if the function can't consume the data packet.
      // It eventually will after one or more frames have been returned in this loop.
      res = dav1d_send_data(dav1dCtx.get(), &data);
      if (res < 0 && res != DAV1D_ERR(EAGAIN)) {
        // A decoding error occurred.
        LOG(ERROR) << "dav1d_send_data(compressedRGBSize: " << frameMetadata.compressedRGBSize << ") returned " << res << " (isKeyframe: " << frameMetadata.isKeyframe << ")";
        dav1d_data_unref(&data); return;
      }
      
      if (res != DAV1D_ERR(EAGAIN)) {
        frameQueue.emplace_back(item->frameIndex, /*isEmpty*/ false, frameMetadata.textureWidth, frameMetadata.textureHeight);
      }
      
      if (quitRequested || abortCurrentFrames) { dav1d_data_unref(&data); return; }
      
      if (!GetPictures(/*atEndOfVideo*/ false, /*pictureReceived*/ nullptr)) {
        dav1d_data_unref(&data); return;
      }
      
      if (quitRequested || abortCurrentFrames) { dav1d_data_unref(&data); return; }
    } while (data.sz);
    
    // Handle end-of-stream by draining all buffered frames
    if (item->frameIndex == frameIndex->GetFrameCount() - 1) {
      bool pictureReceived = false;
      do {
        if (!GetPictures(/*atEndOfVideo*/ true, &pictureReceived)) {
          return;
        }
      } while (pictureReceived && !quitRequested && !abortCurrentFrames);
    }
  }
  
  bool ProcessZStdTexture(WorkItem* item, const u8* textureDataPtr) {
    // Lazily allocate the decompression context
    if (!zstdCtx) {
      zstdCtx.reset(ZSTD_createDCtx(), [](ZSTD_DCtx* ctx) { ZSTD_freeDCtx(ctx); });
    }
    
    // Decompress
    vector<u8> decompressedRGB(item->frameMetadata->textureWidth * item->frameMetadata->textureHeight * 3);
    
    const TimePoint decompressionStartTime = Clock::now();
    const usize decompressedBytes = ZSTD_decompressDCtx(zstdCtx.get(), decompressedRGB.data(), decompressedRGB.size(), textureDataPtr, item->frameMetadata->compressedRGBSize);
    const TimePoint decompressionEndTime = Clock::now();
    
    if (ZSTD_isError(decompressedBytes)) {
      LOG(ERROR) << "Error decompressing the texture with zstd: " << ZSTD_getErrorName(decompressedBytes);
      return false;
    } else if (decompressedBytes != decompressedRGB.size()) {
      LOG(ERROR) << "Obtained unexpected byte count (" << decompressedBytes << ") for decompressed texture, expected to be " << decompressedRGB.size();
      return false;
    }
    if (verboseDecoding) {
      LOG(1) << "Texture decompressed with zstd in " << (MillisecondsDuration(decompressionEndTime - decompressionStartTime).count()) << " ms";
    }
    
    // Submit
    lock_guard<mutex> abortLock(abortMutex);
    if (abortCurrentFrames) { return false; }
    decodingThread->QueueUncompressedRGB(item->frameIndex, std::move(decompressedRGB));
    
    return true;
  }
  
  /// Returns true on success (whether a frame was received or not), false if an error occurred.
  bool GetPictures(bool atEndOfVideo, bool* pictureReceived) {
    auto getEmptyTextureFrames = [this]() {
      while (!frameQueue.empty() && frameQueue.front().isEmpty) {
        if (!OutputEmptyPicture(frameQueue.front().frameIndex)) { return false; }
        frameQueue.erase(frameQueue.begin());
      }
      return true;
    };
    
    if (pictureReceived) { *pictureReceived = false; }
    
    // Get any empty texture frames before the dav1d picture(s)
    if (!getEmptyTextureFrames()) { return false; }
    
    // Get the dav1d picture(s)
    UniqueDav1dPicturePtr picture(new Dav1dPicture());
    memset(picture.get(), 0, sizeof(*picture));
    const int res = dav1d_get_picture(dav1dCtx.get(), picture.get());
    
    if (res >= 0) {
      if (pictureReceived) { *pictureReceived = true; }
      
      if (frameQueue.empty()) {
        LOG(ERROR) << "Got a frame from dav1d" << (atEndOfVideo ? " at the end of the video stream" : "") << ", but frameQueue is empty";
      } else {
        const bool success = OutputPicture(frameQueue.front(), std::move(picture));
        frameQueue.erase(frameQueue.begin());
        if (!success) { return false; }
      }
    } else if (res != DAV1D_ERR(EAGAIN)) {
      // A decoding error occurred.
      LOG(ERROR) << "dav1d_get_picture() " << (atEndOfVideo ? " at the end of the video stream" : "") << "returned " << res;
      frameQueue.erase(frameQueue.begin());
    }
    
    // Get any empty texture frames after the dav1d picture(s)
    if (!getEmptyTextureFrames()) { return false; }
    
    return res >= 0 || res == DAV1D_ERR(EAGAIN);
  }
  
  bool OutputPicture(const FrameBeingDecoded& frame, UniqueDav1dPicturePtr&& picture) {
    // TODO: If OutputPicture() returns false, should we notify the decoding thread to try keeping
    //       the dav1d pictures and the remaining frame data in sync? Currently, a single failure
    //       here would cause the decoding state to become inconsistent between both threads.
    
    if (picture->p.w != frame.textureWidth ||
        picture->p.h != frame.textureHeight) {
      LOG(ERROR) << "Texture size is inconsistent between metadata (" << frame.textureWidth << " x " << frame.textureHeight
                << ") and AV.1 video (" << picture->p.w << " x " << picture->p.h << ")";
      return false;
    }
    
    if (picture->p.layout != DAV1D_PIXEL_LAYOUT_I420) {
      LOG(ERROR) << "Format of decoded AV.1 data is not DAV1D_PIXEL_LAYOUT_I420, but: " << picture->p.layout;
      return false;
    }
    
    if (picture->p.bpc != 8) {
      LOG(ERROR) << "Bits per pixel of decoded AV.1 data is not 8, but: " << picture->p.bpc;
      return false;
    }
    
    lock_guard<mutex> abortLock(abortMutex);
    if (abortCurrentFrames) {
      return false;
    }
    decodingThread->QueueDav1dPicture(frame.frameIndex, std::move(picture));
    return true;
  }
  
  bool OutputEmptyPicture(int frameIndex) {
    lock_guard<mutex> abortLock(abortMutex);
    if (abortCurrentFrames) {
      return false;
    }
    decodingThread->QueueDav1dPicture(frameIndex, nullptr);
    return true;
  }
  
  static int Dav1dAllocPictureCallback(Dav1dPicture* pic, void* cookie) {
    VideoThread<FrameT>* context = static_cast<VideoThread<FrameT>*>(cookie);
    return context->dav1dZeroCopy->Dav1dAllocPictureCallback(pic);
  }
  
  static void Dav1dReleasePictureCallback(Dav1dPicture* pic, void* cookie) {
    VideoThread<FrameT>* context = static_cast<VideoThread<FrameT>*>(cookie);
    return context->dav1dZeroCopy->Dav1dReleasePictureCallback(pic);
  }
  
  static void Dav1dLogCallback(void* /*cookie*/, const char* format, va_list ap) {
    LOG(ERROR) << "dav1d: " << loguru::vstrprintf(format, ap);
  }
  
  static void Dav1dFreeDataCallback(const uint8_t* /*buf*/, void* cookie) {
    shared_ptr<vector<u8>>* frameDataPointerCopy = static_cast<shared_ptr<vector<u8>>*>(cookie);
    delete frameDataPointerCopy;
  }
  
  // Work queue
  mutex workQueueMutex;
  condition_variable newWorkCondition;
  vector<WorkItem*> workQueue;
  int lastFrameIndexQueuedForDecoding = -1;
  
  atomic<bool> abortCurrentFrames;
  mutex abortMutex;
  
  // dav1d zero-copy callback object (not owned)
  Dav1dZeroCopy* dav1dZeroCopy = nullptr;
  
  // dav1d decoding context
  shared_ptr<Dav1dContext> dav1dCtx;
  
  // Queue of frame indices passed to dav1d.
  // Pairs of (frameIndex, isEmpty).
  struct FrameBeingDecoded {
    inline FrameBeingDecoded(
        int frameIndex,
        bool isEmpty,
        u32 textureWidth,
        u32 textureHeight)
        : frameIndex(frameIndex),
          isEmpty(isEmpty),
          textureWidth(textureWidth),
          textureHeight(textureHeight) {}
    
    int frameIndex;
    bool isEmpty;
    u32 textureWidth;
    u32 textureHeight;
  };
  vector<FrameBeingDecoded> frameQueue;
  
  // ZStd context, only allocated upon encountering a zstd-encoded texture
  shared_ptr<ZSTD_DCtx> zstdCtx;
  
  // Worker thread
  atomic<bool> threadRunning;
  atomic<bool> quitRequested;
  std::thread thread;
  
  // Config
  bool verboseDecoding;
  
  // External objects
  DecodingThread<FrameT>* decodingThread;
  FrameIndex* frameIndex;  // not owned
};

}
