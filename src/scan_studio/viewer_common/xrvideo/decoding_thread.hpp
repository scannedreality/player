#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <dav1d/dav1d.h>
#include <../vcs_version.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/xrvideo_file.hpp"

#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/util.hpp"

#include "scan_studio/viewer_common/opengl/context.hpp"

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/transfer_thread.hpp"

namespace scan_studio {
using namespace vis;

/// unique_ptr that correctly unrefs a Dav1dPicture pointer
struct Dav1dPictureDeleter {
  void operator()(Dav1dPicture* p) { dav1d_picture_unref(p); delete p; }
};
typedef unique_ptr<Dav1dPicture, Dav1dPictureDeleter> UniqueDav1dPicturePtr;

/// Helper class that is passed to the Initialize() function of XRVideo frames,
/// allowing them to retrieve their texture frame, if needed blocking until it is available
/// or until an error occurred or decoding was aborted.
class TextureFramePromise {
 public:
  enum class Status {
    Open,
    Fulfilled,
    Aborted
  };
  
  /// Creates an open promise.
  inline TextureFramePromise()
      : status(Status::Open) {}
  
  /// Creates an already-fulfilled promise (dav1d picture variant).
  inline TextureFramePromise(UniqueDav1dPicturePtr&& picture)
      : status(Status::Fulfilled),
        picture(std::move(picture)) {}
  
  /// Creates an already-fulfilled promise (uncompressed RGB variant).
  inline TextureFramePromise(vector<u8>&& rgbData)
      : status(Status::Fulfilled),
        rgbData(std::move(rgbData)) {}
  
  /// Fulfills the promise (dav1d picture variant).
  inline void Fulfill(UniqueDav1dPicturePtr&& picture) {
    unique_lock<mutex> lock(accessMutex);
    
    if (status != Status::Open) {
      LOG(ERROR) << "Fulfill() called on promise that is not open. Status is: " << static_cast<int>(status.load());
      return;
    }
    
    this->picture = std::move(picture);
    status = Status::Fulfilled;
    
    lock.unlock();
    
    fulfilledOrAbortedCondition.notify_all();
  }
  
  /// Fulfills the promise (uncompressed RGB variant).
  inline void Fulfill(vector<u8>&& rgbData) {
    unique_lock<mutex> lock(accessMutex);
    
    if (status != Status::Open) {
      LOG(ERROR) << "Fulfill() called on promise that is not open. Status is: " << static_cast<int>(status.load());
      return;
    }
    
    this->rgbData = std::move(rgbData);
    status = Status::Fulfilled;
    
    lock.unlock();
    
    fulfilledOrAbortedCondition.notify_all();
  }
  
  /// Aborts the promise.
  inline void Abort() {
    unique_lock<mutex> lock(accessMutex);
    
    if (status != Status::Open) {
      LOG(ERROR) << "Abort() called on promise that is not open. Status is: " << static_cast<int>(status.load());
      return;
    }
    
    status = Status::Aborted;
    
    lock.unlock();
    
    fulfilledOrAbortedCondition.notify_all();
  }
  
  /// Waits for the promise to be fulfilled or aborted.
  /// Returns true if it got fulfilled, false if aborted.
  inline bool Wait() {
    unique_lock<mutex> lock(accessMutex);
    
    while (status == Status::Open) {
      fulfilledOrAbortedCondition.wait(lock);
    }
    
    return status == Status::Fulfilled;
  }
  
  /// Returns the current status of the promise without blocking.
  inline Status GetStatus() {
    return status;
  }
  
  /// Takes the result of the promise (dav1d picture variant).
  /// Must only be called if the promise is fulfilled (i.e., Wait() returned true).
  inline UniqueDav1dPicturePtr Take() {
    return std::move(picture);
  }
  
  /// Takes the result of the promise (uncompressed RGB variant).
  /// Must only be called if the promise is fulfilled (i.e., Wait() returned true).
  inline void TakeRGB(vector<u8>* frameData) {
    *frameData = std::move(rgbData);
  }
  
 private:
  mutex accessMutex;
  atomic<Status> status;
  condition_variable fulfilledOrAbortedCondition;
  
  // Variant 1: Dav1d picture
  UniqueDav1dPicturePtr picture;
  
  // Variant 2: RGB data as vector<u8>
  vector<u8> rgbData;
};

template <typename FrameT>
class DecodingThread {
 public:
  ~DecodingThread() {
    Destroy();
  }
  
  void Destroy() {
    WaitForThreadToExit();
    
    ClearQueues();
  }
  
  /// Configures the thread to use OpenGL. It takes ownership of the passed-in context object.
  void SetUseOpenGLContext(unique_ptr<GLContext>&& context) {
    workerThreadOpenGLContext = std::move(context);
  }
  
  void StartThread(bool verboseDecoding, TransferThread<FrameT>* transferThread) {
    if (thread.joinable()) { thread.join(); }
    
    this->verboseDecoding = verboseDecoding;
    this->transferThread = transferThread;
    
    threadRunning = true;
    quitRequested = false;
    threadInitialized = false;
    currentTextureFramePromise = nullptr;
    thread = std::thread(std::bind(&DecodingThread::ThreadMain, this));
  }
  
  /// Waits until the worker thread initialized.
  /// This function is provided to be able to wait until the worker thread called SDL_GL_MakeCurrent(),
  /// since this function is not thread-safe. I am not sure whether it also conflicts with other OpenGL
  /// calls, but to be on the safe side, we make the main thread wait for it.
  /// Returns true if the worker thread initialized successfully, false if there was an error during
  /// its initialization.
  bool WaitForThreadToInitialize() {
    unique_lock<mutex> lock(threadInitializationMutex);
    while (!threadInitialized) {
      threadInitializedCondition.wait(lock);
    }
    return threadInitializedSuccessfully;
  }
  
  void RequestThreadToExit() {
    // NOTE: Never lock the two mutexes below in the opposite order, or there will be the chance of a deadlock.
    workQueueMutex.lock();
    dav1dPictureQueueMutex.lock();
    
    // See the comments in ClearQueues().
    abortCurrentFrame = true;
    if (currentTextureFramePromise && currentTextureFramePromise->GetStatus() == TextureFramePromise::Status::Open) {
      currentTextureFramePromise->Abort();
      currentTextureFramePromise = nullptr;
    }
    
    quitRequested = true;
    
    dav1dPictureQueueMutex.unlock();
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
  
  /// Called by the reading thread.
  /// Attempts to queue the given frameData for decoding into the given cacheItem.
  /// Returns true on success, false if the frame is a dependent frame, and the decoding state
  /// after working through the current workQueue will not be suitable to decode it (because a
  /// different frame than its predecessor will have been decoded last at that point). This may even
  /// happen if always queueing frames in order, because the workQueue might get cleared in-between.
  bool QueueFrame(
      int frameIndex,
      const shared_ptr<XRVideoFrameMetadata>& frameMetadata,
      const shared_ptr<vector<u8>>& frameData,
      const u8* frameContentPtr,
      s64 readingTime,
      WriteLockedCachedFrame<FrameT>&& cacheItem) {
    unique_lock<mutex> lock(workQueueMutex);
    
    if (!frameMetadata->isKeyframe && frameIndex != lastFrameIndexQueuedForDecoding + 1) {
      if (verboseDecoding) {
        LOG(WARNING) << "DecodingThread: Failed to queue a frame, isKeyframe: " << frameMetadata->isKeyframe
                     << ", frameIndex: " << frameIndex << ", lastFrameIndexQueuedForDecoding: " << lastFrameIndexQueuedForDecoding;
      }
      cacheItem.Invalidate();
      return false;
    }
    
    WorkItem* newItem = new WorkItem();
    newItem->frameIndex = frameIndex;
    newItem->frameMetadata = frameMetadata;
    newItem->frameData = frameData;
    newItem->frameContentPtr = frameContentPtr;
    newItem->readingTime = readingTime;
    newItem->cacheItem = std::move(cacheItem);
    newItem->lastFrameIndexQueuedForDecoding = lastFrameIndexQueuedForDecoding;
    workQueue.push_back(newItem);
    
    lastFrameIndexQueuedForDecoding = frameIndex;
    
    lock.unlock();
    newWorkCondition.notify_one();
    
    return true;
  }
  
  /// Called by the video thread to enqueue a decoded Dav1dPicture with the given frameIndex.
  /// The picture then needs to be matched up with the other frame data.
  /// See also: QueueUncompressedRGB().
  void QueueDav1dPicture(int frameIndex, UniqueDav1dPicturePtr&& picture) {
    unique_lock<mutex> lock(dav1dPictureQueueMutex);
    
    // If ProcessItem() already created a promise for the picture that we just received,
    // then use the picture to fulfill that promise.
    // Otherwise, queue the picture to be picked up by ProcessItem() later.
    if (currentTextureFramePromise) {
      if (currentTextureFramePromiseFrameIndex == frameIndex) {
        currentTextureFramePromise->Fulfill(std::move(picture));
        currentTextureFramePromise = nullptr;
      } else {
        // TODO: What would be the best way to react to that, to maximize the chance that future decoding will be consistent again?
        //       Discard our frame, discard the texture frame, discard both, or a complete flush of the pipeline?
        //       I think that flushing the complete pipeline would be needed. For now, for simplicity we only discard both current items.
        LOG(ERROR) << "Mismatch between the decoding thread's next item (" << currentTextureFramePromiseFrameIndex << ") and the next queued dav1d picture (" << frameIndex << ")";
        currentTextureFramePromise->Abort();
        currentTextureFramePromise = nullptr;
      }
    } else {
      Dav1dPictureQueueItem* newItem = new Dav1dPictureQueueItem();
      newItem->frameIndex = frameIndex;
      newItem->picture = std::move(picture);
      dav1dPictureQueue.push_back(newItem);
    }
  }
  
  /// Called by the video thread to enqueue uncompressed RGB data with the given frameIndex.
  /// The picture then needs to be matched up with the other frame data.
  /// See also: QueueDav1dPicture().
  void QueueUncompressedRGB(int frameIndex, vector<u8>&& rgbData) {
    unique_lock<mutex> lock(dav1dPictureQueueMutex);
    
    // If ProcessItem() already created a promise for the picture that we just received,
    // then use the picture to fulfill that promise.
    // Otherwise, queue the picture to be picked up by ProcessItem() later.
    if (currentTextureFramePromise) {
      if (currentTextureFramePromiseFrameIndex == frameIndex) {
        currentTextureFramePromise->Fulfill(std::move(rgbData));
        currentTextureFramePromise = nullptr;
      } else {
        // TODO: What would be the best way to react to that, to maximize the chance that future decoding will be consistent again?
        //       Discard our frame, discard the texture frame, discard both, or a complete flush of the pipeline?
        //       I think that flushing the complete pipeline would be needed. For now, for simplicity we only discard both current items.
        LOG(ERROR) << "Mismatch between the decoding thread's next item (" << currentTextureFramePromiseFrameIndex << ") and the next queued dav1d picture (" << frameIndex << ")";
        currentTextureFramePromise->Abort();
        currentTextureFramePromise = nullptr;
      }
    } else {
      Dav1dPictureQueueItem* newItem = new Dav1dPictureQueueItem();
      newItem->frameIndex = frameIndex;
      newItem->rgbData = std::move(rgbData);
      dav1dPictureQueue.push_back(newItem);
    }
  }
  
  void ClearQueues() {
    // NOTE: Never lock the two mutexes below in the opposite order, or there will be the chance of a deadlock.
    lock_guard<mutex> lock(workQueueMutex);
    {
      lock_guard<mutex> lock(dav1dPictureQueueMutex);
      
      // abortCurrentFrame is required to ensure that, after ClearQueues() returns, we won't create
      // a new, empty promise for an in-progress frame that won't have matching texture data (since we removed
      // that texture data here).
      abortCurrentFrame = true;
      
      // If the thread already created a promise and it is not fulfilled yet, abort it.
      // Since we hold dav1dPictureQueueMutex, we can be sure that the promise is not being fulfilled at the same time.
      // However, in case the promise is already fulfilled, this means that the frame may be being decoded right now,
      // so in that case we simply leave it as it is.
      if (currentTextureFramePromise && currentTextureFramePromise->GetStatus() == TextureFramePromise::Status::Open) {
        currentTextureFramePromise->Abort();
        currentTextureFramePromise = nullptr;
      }
      
      for (Dav1dPictureQueueItem* item : dav1dPictureQueue) {
        delete item;
      }
      dav1dPictureQueue.clear();
    }
    
    if (!workQueue.empty()) {
      lastFrameIndexQueuedForDecoding = workQueue.front()->lastFrameIndexQueuedForDecoding;
    }
    
    for (WorkItem* item : workQueue) {
      item->cacheItem.Invalidate();
      delete item;
    }
    workQueue.clear();
  }
  
  inline int GetLastFrameIndexQueuedForDecoding() {
    workQueueMutex.lock();
    const int result = lastFrameIndexQueuedForDecoding;
    workQueueMutex.unlock();
    return result;
  }
  
 private:
  struct WorkItem {
    /// Index of the frame.
    int frameIndex;
    
    /// Metadata of the frame to decode.
    shared_ptr<XRVideoFrameMetadata> frameMetadata;
    
    /// The compressed frame data to decode.
    shared_ptr<vector<u8>> frameData;
    
    /// Pointer to the start of the frame's encoded content (within the frameData buffer).
    const u8* frameContentPtr;
    
    /// Time in nanoseconds that it took to read the compressed frame data.
    s64 readingTime;
    
    /// Pointer to the cache item in which to store the decoded data,
    /// or a null item in case we only need to decode the frame in order to advance the decoding
    /// state and be able to decode its successive dependent frames.
    WriteLockedCachedFrame<FrameT> cacheItem;
    
    /// The last frame index queued for decoding before this frame.
    /// This is used in case we later remove this frame from the queue again:
    /// Then, we know that after decoding all previous queue items, the decoding state
    /// will be at this frame index.
    int lastFrameIndexQueuedForDecoding;
  };
  
  struct Dav1dPictureQueueItem {
    /// Index of the picture's frame
    int frameIndex;
    
    /// Variant 1: The queued picture
    UniqueDav1dPicturePtr picture;
    
    /// Variant 2: The queued uncompressed RGB data
    vector<u8> rgbData;
  };
  
  void ThreadMain() {
    const bool initializedSuccessfully = InitializeWorkerThread();
    
    threadInitializationMutex.lock();
    threadInitialized = true;
    threadInitializedSuccessfully = initializedSuccessfully;
    threadInitializationMutex.unlock();
    threadInitializedCondition.notify_all();
    
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
      
      abortCurrentFrame = false;
      
      lock.unlock();
      
      ProcessItem(item);
      
      delete item;
    }
    
    DeinitializeWorkerThread();
    threadRunning = false;
  }
  
  bool InitializeWorkerThread() {
    SCAN_STUDIO_SET_THREAD_NAME("scan-decoding");
    
    // Attention: According to the following post, SDL_GL_MakeCurrent() is not thread-safe on all platforms:
    //            https://stackoverflow.com/questions/64484835/how-to-setup-one-shared-opengl-contexts-per-thread-with-sdl2
    if (workerThreadOpenGLContext && !workerThreadOpenGLContext->MakeCurrent()) {
      LOG(ERROR) << "Failed to make workerThreadOpenGLContext current";
      return false;
    }
    
    if (!decodingContext.Initialize()) { return false; }
    
    return true;
  }
  
  void DeinitializeWorkerThread() {
    decodingContext.Destroy();
    
    // We do not delete the context anymore.
    // This way, the context survives restarts of the DecodingThread,
    // which happen when an XRVideo object switches to a different video.
    // workerThreadOpenGLContext.reset();
  }
  
  void ProcessItem(WorkItem* item) {
    // If the frame's dav1d picture was already decoded, create an already-fulfilled promise for it.
    // Otherwise, create a promise that will be fulfilled later by QueueDav1dPicture() (or aborted).
    TextureFramePromise textureFramePromise;
    {
      unique_lock<mutex> lock(dav1dPictureQueueMutex);
      
      if (abortCurrentFrame) {
        item->cacheItem.Invalidate();
        return;
      }
      
      if (dav1dPictureQueue.empty()) {
        // The picture is not available yet. Set a pointer to it so it can be fulfilled (or aborted) later.
        currentTextureFramePromise = &textureFramePromise;
        currentTextureFramePromiseFrameIndex = item->frameIndex;
      } else if (dav1dPictureQueue.front()->frameIndex == item->frameIndex) {
        // The picture is already available. Create a promise that is already fulfilled.
        if (dav1dPictureQueue.front()->picture) {
          textureFramePromise.Fulfill(std::move(dav1dPictureQueue.front()->picture));
        } else {
          textureFramePromise.Fulfill(std::move(dav1dPictureQueue.front()->rgbData));
        }
        delete dav1dPictureQueue.front();
        dav1dPictureQueue.erase(dav1dPictureQueue.begin());
      } else {
        // A picture is available, but it is for the wrong frame.
        // TODO: What would be the best way to react to that, to maximize the chance that future decoding will be consistent again?
        //       Discard our frame, discard the texture frame, discard both, or a complete flush of the pipeline?
        //       I think that flushing the complete pipeline would be needed. For now, for simplicity we only discard both current items.
        LOG(ERROR) << "Mismatch between the decoding thread's next item (" << item->frameIndex << ") and the next queued dav1d picture (" << dav1dPictureQueue.front()->frameIndex << ")";
        delete dav1dPictureQueue.front();
        dav1dPictureQueue.erase(dav1dPictureQueue.begin());
        item->cacheItem.Invalidate();
        return;
      }
    }
    
    if (item->cacheItem.GetFrame() != nullptr) {
      // Decode the frame into a cache item
      const TimePoint decodingStartTime = Clock::now();
      
      if (!item->cacheItem.GetFrame()->Initialize(*item->frameMetadata, item->frameContentPtr, &textureFramePromise, &decodingContext, verboseDecoding)) {
        // This does happen if we abort the textureFramePromise when the video is seeked. In that case, it is not an error.
        // LOG(ERROR) << "Failed to initialize an XRVideo frame";

        if (textureFramePromise.GetStatus() == TextureFramePromise::Status::Open) {
          // This happens if the frame fails to initialize before the texture frame promise has been fulfilled.
          // In that case, we must wait for the promise to be fulfilled, since textureFramePromise is a local variable here,
          // and the VideoThread would try to call Fulfill() (or Abort()) on it after it was destructed otherwise.
          textureFramePromise.Wait();
        }

        item->cacheItem.Invalidate();
        return;
      }
      
      // To simulate a long decoding time:
      // this_thread::sleep_for(100ms);
      
      const TimePoint decodingEndTime = Clock::now();
      const s64 decodingTime = NanosecondsFromTo(decodingStartTime, decodingEndTime);
      
      transferThread->QueueFrame(item->frameIndex, item->readingTime, decodingTime, move(item->cacheItem));
      
      if (verboseDecoding) {
        LOG(1) << "DecodingThread: Decoded frame " << item->frameIndex << " in " << MillisecondsFromTo(decodingStartTime, decodingEndTime) << " ms";
      }
    } else {
      // (Partially) decode the frame only to advance the decoding state
      textureFramePromise.Wait();
    }
  }
  
  // Decoding context (common to all render paths)
  XRVideoDecodingContext decodingContext;
  
  // Work queue
  mutex workQueueMutex;
  condition_variable newWorkCondition;
  vector<WorkItem*> workQueue;
  int lastFrameIndexQueuedForDecoding = -1;
  atomic<bool> abortCurrentFrame;
  
  // Dav1d picture queue
  mutex dav1dPictureQueueMutex;
  vector<Dav1dPictureQueueItem*> dav1dPictureQueue;
  TextureFramePromise* currentTextureFramePromise = nullptr;
  int currentTextureFramePromiseFrameIndex = -1;
  
  // OpenGL context for the worker thread
  unique_ptr<GLContext> workerThreadOpenGLContext;
  
  // Worker thread initialization
  mutex threadInitializationMutex;
  bool threadInitialized;
  bool threadInitializedSuccessfully;
  condition_variable threadInitializedCondition;
  
  // Worker thread
  atomic<bool> threadRunning;
  atomic<bool> quitRequested;
  std::thread thread;
  
  // Config
  bool verboseDecoding;
  
  // External objects
  TransferThread<FrameT>* transferThread;
};

}
