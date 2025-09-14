#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/xrvideo_file.hpp"

#include "scan_studio/viewer_common/timing.hpp"
#include "scan_studio/viewer_common/util.hpp"

#include "scan_studio/viewer_common/opengl/context.hpp"

#include "scan_studio/viewer_common/xrvideo/decoded_frame_cache.hpp"
#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

namespace scan_studio {

template <typename FrameT>
class TransferThread {
 public:
  ~TransferThread() {
    Destroy(/*finishAllTransfers*/ false);
  }
  
  void Destroy(bool finishAllTransfers) {
    WaitForThreadToExit();
    
    ClearQueue(finishAllTransfers);
  }
  
  /// Configures the thread to use OpenGL. It takes ownership of the passed-in context object.
  void SetUseOpenGLContext(unique_ptr<GLContext>&& context) {
    workerThreadOpenGLContext = std::move(context);
  }
  
  void StartThread(bool verboseDecoding) {
    if (thread.joinable()) { thread.join(); }
    
    this->verboseDecoding = verboseDecoding;
    
    threadRunning = true;
    quitRequested = false;
    threadInitialized = false;
    thread = std::thread(std::bind(&TransferThread::ThreadMain, this));
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
  
  void QueueFrame(int frameIndex, s64 readingTime, s64 decodingTime, WriteLockedCachedFrame<FrameT>&& cacheItem) {
    WorkItem* newItem = new WorkItem();
    newItem->frameIndex = frameIndex;
    newItem->readingTime = readingTime;
    newItem->decodingTime = decodingTime;
    newItem->cacheItem = move(cacheItem);
    
    workQueueMutex.lock();
    workQueue.push_back(newItem);
    workQueueMutex.unlock();
    
    newWorkCondition.notify_one();
  }
  
  void ClearQueue(bool finishAllTransfers) {
    workQueueMutex.lock();
    for (WorkItem* item : workQueue) {
      if (finishAllTransfers) {
        item->cacheItem.GetFrame()->WaitForResourceTransfers();
      }
      item->cacheItem.Invalidate();
      delete item;
    }
    workQueue.clear();
    workQueueMutex.unlock();
  }
  
  void GetAverageDecodingTime(int* sampleCount, s64* averageTimeNSec) {
    averageDecodingTimeMutex.lock();
    *sampleCount = averageDecodingTimeSampleCount;
    *averageTimeNSec = averageDecodingTimeNSec;
    averageDecodingTimeMutex.unlock();
  }
  
 private:
  struct WorkItem {
    /// Index of the frame (used for debug logging only).
    int frameIndex;
    
    /// Time in nanoseconds that it took to read the compressed frame data.
    s64 readingTime;
    
    /// Time in nanoseconds that it took to decode the compressed frame data.
    s64 decodingTime;
    
    /// Pointer to the cache item in which to store the decoded data,
    /// or nullptr in case we only need to decode the frame in order to advance the decoding
    /// state and be able to decode its successive dependent frames.
    WriteLockedCachedFrame<FrameT> cacheItem;
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
      
      lock.unlock();
      
      ProcessItem(item);
      
      delete item;
    }
    
    DeinitializeWorkerThread();
    threadRunning = false;
  }
  
  bool InitializeWorkerThread() {
    SCAN_STUDIO_SET_THREAD_NAME("scan-transfer");
    
    // Attention: According to the following post, SDL_GL_MakeCurrent() is not thread-safe on all platforms:
    //            https://stackoverflow.com/questions/64484835/how-to-setup-one-shared-opengl-contexts-per-thread-with-sdl2
    if (workerThreadOpenGLContext && !workerThreadOpenGLContext->MakeCurrent()) {
      LOG(ERROR) << "Failed to make workerThreadOpenGLContext current";
      return false;
    }
    
    return true;
  }
  
  void DeinitializeWorkerThread() {
    // We do not delete the context anymore.
    // This way, the context survives restarts of the TransferThread,
    // which happen when an XRVideo object switches to a different video.
    // workerThreadOpenGLContext.reset();
  }
  
  void ProcessItem(WorkItem* item) {
    if (item->cacheItem.GetFrame() != nullptr) {
      // Note that the decodingThread already started the transfer, and we don't know when exactly the GPU commands
      // to do the transfers start to execute. So, this here is only a rough estimate of the transfer time.
      const TimePoint transferStartTime = Clock::now();
      
      item->cacheItem.GetFrame()->WaitForResourceTransfers();
      item->cacheItem.Unlock();
      
      const TimePoint transferEndTime = Clock::now();
      const s64 transferTimeEstimate = NanosecondsFromTo(transferStartTime, transferEndTime);
      
      // Update the average frame decoding time.
      // We assume that frame reading, decoding, and transfer may run in parallel.
      // Thus, the effective decoding time (disregarding the first frame's latency) is the maximum of these times.
      const s64 effectiveDecodingTime = max(item->readingTime, max(item->decodingTime, transferTimeEstimate));
      if (verboseDecoding) {
        LOG(1) << "TransferThread: Transferred frame " << item->frameIndex << " in " << MillisecondsFromTo(transferStartTime, transferEndTime) << " ms; effective decoding time: "
               << (effectiveDecodingTime / (1000.0 * 1000.0)) << " ms";
      }
      UpdateAverageDecodingTime(effectiveDecodingTime);
    }
  }
  
  void UpdateAverageDecodingTime(s64 newSample) {
    if (averageDecodingTimeSamples.size() >= maxNumAverageDecodingTimeSamples) {
      averageDecodingTimeSamples.erase(averageDecodingTimeSamples.begin());
    }
    averageDecodingTimeSamples.push_back(newSample);
    
    s64 averageDecodingTimeSum = 0;
    for (s64 value : averageDecodingTimeSamples) {
      averageDecodingTimeSum += value;
    }
    
    averageDecodingTimeMutex.lock();
    averageDecodingTimeSampleCount = averageDecodingTimeSamples.size();
    averageDecodingTimeNSec = averageDecodingTimeSum / averageDecodingTimeSampleCount;
    averageDecodingTimeMutex.unlock();
    
    if (verboseDecoding) {
      LOG(1) << "TransferThread: Average decoding time: " << (averageDecodingTimeSum / (averageDecodingTimeSampleCount * 1000.0 * 1000.0)) << " ms, sample count: " << averageDecodingTimeSamples.size();
    }
  }
  
  // Statistics: Average decoding time
  static constexpr int maxNumAverageDecodingTimeSamples = 32;
  vector<s64> averageDecodingTimeSamples;
  
  mutex averageDecodingTimeMutex;  // protects the two attributes below
  int averageDecodingTimeSampleCount = 0;
  s64 averageDecodingTimeNSec = 0;
  
  // Work queue
  mutex workQueueMutex;
  condition_variable newWorkCondition;
  vector<WorkItem*> workQueue;
  
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
};

}
