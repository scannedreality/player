#pragma once

#include <thread>

#include <libvis/io/input_stream.h>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/wrap_mutex.hpp"

#include "scan_studio/viewer_common/http_request.hpp"

namespace scan_studio {
using namespace vis;

/// Implementation of InputStream for files streamed from the network.
///
/// The streaming strategy of this class is as follows:
/// - Since the optimal strategy requires knowledge of where future reads will likely be,
///   and this class does not have this knowledge, it is possible to externally request streaming a particular range.
///   For certain types of files, this also allows to stream ranges in such a way that data accesses will never
///   cross a range boundary, allowing for zero-copy use of the streamed data. This is for example the case in XRV files
///   as they consist of a series of chunks, and all requested ranges may be aligned with chunk boundaries (once we know the file index).
/// - If Read() is called on a part of the file that is not cached and not being streamed,
///   then the class will force a range to be streamed that covers the Read() range,
///   or possibly some slightly larger range; a minimum streamed packet size may be defined for this
///   (although it will not be possible to adhere to this minimum in all situations given that ranges
///   may be streamed manually, possibly leading to gaps that are smaller than this minimum size).
///   This mechanism is generally intended to prevent stalling if the external code does not cause the correct ranges
///   to be streamed, but since it only requests ranges when it is basically already too late, it must not be relied
///   on for well-performing streaming.
///
/// All streamed ranges are cached.
/// A maximum cache size may be set, which causes the class to drop least-recently-used ranges.
///
/// TODO: For each new request, there is the ping overhead: it travels to the server, is handled there, and finally the headers for the request arrive back at the sender (us).
///       The actual content receiving only starts after this period of latency.
///       For better streaming performance, it seems that it would be important to start a second request before the current request finishes,
///       trying to avoid any waiting time where we only wait for the next request's headers, but do not transfer any data!
///
/// TODO: Using this class to stream XRV files, currently we copy memory when reading data.
///       In order to achieve zero-copy operation, consider:
///       - Make it possible to externally pin ranges in order to ensure that they won't be dropped
///         (allowing for zero-copy use of the pinned data). Notice that this may cause the maximum cache size to be exceeded when pinning too many ranges.
///       - We first read the headers while not knowing about the file structure yet, and later read the chunks, while knowing all chunk boundaries exactly.
///         For this use-case, it may be useful to 'retire' all current ranges after finishing reading the headers,
///         allowing all future ranges to be aligned with chunk boundaries, which enables zero-copy use of the chunk data.
///         The retired ranges may still be kept for a while and be used to memcpy() data into new ranges if they overlap with new ranges,
///         such that only the actually unknown ranges need to be streamed.
///         Alternatively, if the consuming code can transparently handle both the case that memory is copied and the case of zero-copy access,
///         then no such explicit retiring may be necessary.
class StreamingInputStream : public InputStream {
 public:
  inline StreamingInputStream() {}
  
  StreamingInputStream(const StreamingInputStream& other) = delete;
  StreamingInputStream& operator= (const StreamingInputStream& other) = delete;
  
  StreamingInputStream(StreamingInputStream&& other) = delete;
  StreamingInputStream& operator= (StreamingInputStream&& other) = delete;
  
  ~StreamingInputStream();
  
  /// Connects to the given URI for streaming.
  ///
  /// This will send an HTTP HEAD request in order to determine the size of the file.
  /// Open() will return asynchronously, without waiting for this request to complete.
  ///
  /// Notice that maxCacheSize is treated as a guideline and not as a strict maximum.
  ///
  /// TODO: We could make an optional parameter that allows to pass the file size if already known.
  ///       Then the HEAD request could be skipped for slightly better performance.
  bool Open(const char* uri, s64 minStreamSize, s64 maxCacheSize, bool allowUntrustedCertificates, unique_ptr<HttpRequestFactory>&& httpRequestFactory);
  
  /// Closes the stream if it is open.
  void Close();
  
  /// Returns true if there was an unrecoverable streaming error.
  bool HasFatalError();
  
  /// Requests the given range of the file to be streamed.
  /// If the range overlaps with existing ranges, it will be clamped / broken up / discarded, adding only new parts that are not available or already scheduled yet.
  /// If allowExtendRange is true, the first and last range may be extended to try to increase their size up to minStreamSize, in order to avoid tiny packets with comparatively too large overhead.
  /// If maxStreamSize is larger than zero, then ranges will be subdivided such that each resulting element is at most of this size,
  /// in order to avoid very large packets (for the case where packets that are in progress can't be used yet, which is currently the case).
  void StreamRange(s64 from, s64 to, bool allowExtendRange, s64 maxStreamSize);
  
  /// Drops all pending streaming requests, except if a Read() call is in progress and they are required to fulfill that read.
  /// This may be used if the knowledge about future reads changes, for example, when the user seeks to a different position in a video file.
  void DropPendingRequests();
  
  // InputStream implementation
  virtual usize Read(void* data, usize size) override;
  virtual void AbortRead() override;
  virtual bool Seek(u64 offsetFromStart) override;
  virtual u64 SizeInBytes() override;
  
 private:
  struct CachedRange {
    inline CachedRange() {}
    
    inline CachedRange(unique_ptr<HttpRequest>&& range, u64 scheduleCounter, bool isProtected)
        : range(std::move(range)),
          lastAccess(-1),
          scheduleCounter(scheduleCounter),
          isProtected(isProtected) {}
    
    inline CachedRange(CachedRange&& other) = default;
    inline CachedRange& operator=(CachedRange&& other) = default;
    
    inline CachedRange(const CachedRange& other) = delete;
    inline CachedRange& operator=(const CachedRange& other) = delete;
    
    /// Contains the downloaded cached data
    /// as well as its range information (ContentRangeFrom() to ContentRangeTo()).
    unique_ptr<HttpRequest> range;
    
    /// Value of the access counter when this range was last accessed by a read operation.
    /// Used to clean up the ranges that have been accessed the longest ago.
    /// If this range has never been read, this is set to -1.
    s64 lastAccess;
    
    /// Counter for when this range was scheduled.
    /// Used to protect ranges that have been scheduled after the one that was last read from being cleaned up.
    u64 scheduleCounter;
    
    /// Is this range required to fulfill an ongoing Read() call?
    /// If yes, it is protected from being removed by the cache cleanup routine.
    bool isProtected;
  };
  
  struct ScheduledRange {
    inline ScheduledRange() {}
    
    inline ScheduledRange(s64 from, s64 to, u64 scheduleCounter, bool isProtected)
        : from(from),
          to(to),
          scheduleCounter(scheduleCounter),
          isProtected(isProtected) {}
    
    s64 from;
    s64 to;
    
    /// See the same attribute in CachedRange.
    u64 scheduleCounter;
    
    /// Is this range required to fulfill an ongoing Read() call?
    /// If yes, it is protected from being removed by calls to DropPendingRequests().
    bool isProtected;
  };
  
  struct Ranges {
    /// Already-downloaded, cached ranges, ordered by increasing file position.
    ///
    /// These are stored as HttpRequest because the request class
    /// may need to allocate the response content memory externally to be able to prevent memcopies.
    /// So, we have to keep the request object around as long as we want to access the response content without copying.
    vector<CachedRange> cachedRanges;
    
    /// The range currently being downloaded, or null if there is no active download.
    unique_ptr<HttpRequest> currentRange;
    ScheduledRange currentScheduledRange;
    
    /// Ranges scheduled for future download, in the order in which they will be downloaded
    /// (unless re-prioritization happens).
    vector<ScheduledRange> scheduledRanges;
  };
  
  s64 FindPreviousRangeEnd(s64 position, LockedWrapMutex<Ranges>* rangesLock);
  s64 FindNextRangeStart(s64 position, LockedWrapMutex<Ranges>* rangesLock);
  
  ScheduledRange ScheduleRange(s64 from, s64 to, bool allowExtendRange, bool bypassQueue, bool protectRange, LockedWrapMutex<Ranges>* rangesLock);
  
  bool WaitForHeadRequest();
  
  void StartDownload(const ScheduledRange& range, LockedWrapMutex<Ranges>* rangesLock);
  void RetryThreadMain();
  void DownloadFinishedOrFailedCallback(HttpRequest* request, bool success);
  static void DownloadFinishedOrFailedCallbackStatic(HttpRequest* request, bool success, void* userPtr);
  
  bool StartHeadRequest();
  void HeadRetryThreadMain();
  void HeadFinishedOrFailedCallback(HttpRequest* request, bool success);
  static void HeadFinishedOrFailedCallbackStatic(HttpRequest* request, bool success, void* userPtr);
  
  /// Once completed successfully, contains the file size information
  unique_ptr<HttpRequest> headRequest;
  mutex headRequestSuccessfulMutex;
  condition_variable headRequestSuccessfulCondition;
  atomic<bool> headRequestSuccessful = false;
  
  /// Wraps all (cached, current, and scheduled) ranges in a mutex
  WrapMutex<Ranges> ranges;
  
  /// This condition is triggered when either a new range has finished downloading,
  /// or a fatal streaming error occurred.
  condition_variable newRangeCondition;
  
  /// Whether a fatal error occurred during streaming.
  /// This is the case if we get a different content range from the server than we request.
  /// This implies that the file has been truncated on the server after streaming started.
  bool fatalErrorOccurred = false;
  
  /// Whether the current Read() shall be aborted.
  atomic<bool> abortCurrentRead = false;
  
  /// Current file position for the InputStream implementation
  s64 filePosition = 0;
  
  /// Access counter for read operations, used as cache cleanup criterion
  u64 accessCounter = 0;
  
  /// Range scheduling counter, used to protect ranges that were scheduled after the range that was last read.
  u64 scheduleCounter = 0;
  
  /// A thread which is started after a download fails. It re-tries the download after a short delay.
  std::thread retryThread;
  std::thread headRetryThread;
  
  /// This may be signaled to abort the retry threads.
  condition_variable abortRetryCondition;
  mutex abortRetryMutex;
  
  /// These are helper objects that we need for cleanly shutting down while avoiding deadlocks.
  mutex callbackMutex;
  atomic<bool> shuttingDown = false;
  
  // Configuration
  s64 minStreamSize = -1;
  s64 maxCacheSize = -1;
  string uri;
  bool allowUntrustedCertificates;
  unique_ptr<HttpRequestFactory> httpRequestFactory;
};

}
