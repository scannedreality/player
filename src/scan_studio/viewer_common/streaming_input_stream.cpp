#include "scan_studio/viewer_common/streaming_input_stream.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

#include <loguru.hpp>

namespace scan_studio {

constexpr bool kDebug = false;
constexpr bool kDebugLogStatistics = false;

StreamingInputStream::~StreamingInputStream() {
  Close();
}

bool StreamingInputStream::Open(const char* uri, s64 minStreamSize, s64 maxCacheSize, bool allowUntrustedCertificates, unique_ptr<HttpRequestFactory>&& httpRequestFactory) {
  if (kDebug) { LOG(1) << "StreamingInputStream: Open() uri: " << uri << ", minStreamSize: " << minStreamSize << ", maxCacheSize: " << maxCacheSize; }
  
  Close();
  shuttingDown = false;
  fatalErrorOccurred = false;
  
  this->uri = uri;
  this->minStreamSize = minStreamSize;
  this->maxCacheSize = maxCacheSize;
  this->allowUntrustedCertificates = allowUntrustedCertificates;
  this->httpRequestFactory = std::move(httpRequestFactory);
  
  if (!StartHeadRequest()) {
    if (kDebug) { LOG(WARNING) << "Head request failed immediately, starting retry thread ..."; }
    headRetryThread = std::thread(&StreamingInputStream::HeadRetryThreadMain, this);
  }
  
  return true;
}

void StreamingInputStream::Close() {
  if (kDebug) { LOG(1) << "StreamingInputStream: Close()"; }
  
  // Abort all ongoing requests, and deallocate all cached requests to free up memory.
  //
  // Notice that destructing a HttpRequest will wait for its completion callback to finish
  // in case it is running. Since we lock rangesLock both here while destructing the HttpRequests,
  // as well as in their completion callback, this could lead to a deadlock where the HttpRequest
  // waits for the callback to finish, but it cannot finish because it cannot acquire the mutex that
  // we are holding here!
  // The way we fix this is a bit cumbersome: We introduced another mutex `callbackMutex` that we hold during the whole
  // completion callback. We first lock that mutex here, set the `shuttingDown` flag to true, and unlock it again.
  // The completion callback checks this `shuttingDown` flag before trying to acquire rangesLock; if it is
  // set, it returns right away. This way, we can be sure that the callback will finish running despite
  // holding rangesLock ourselves.
  {
    lock_guard<mutex> callbackLock(callbackMutex);
    shuttingDown = true;
  }
  
  {
    lock_guard<mutex> lock(headRequestSuccessfulMutex);
  }
  headRequestSuccessfulCondition.notify_all();
  
  {
    auto rangesLock = ranges.Lock();
    
    headRequest.reset();
    
    // This will also keep the retryThread from re-starting currentRange:
    rangesLock->currentRange.reset();
    
    // Free up memory:
    rangesLock->cachedRanges = vector<CachedRange>();
    rangesLock->scheduledRanges = vector<ScheduledRange>();
  }
  
  // The currentRange.reset() above keeps the retryThread from creating new requests, i.e., we have cleaned up all requests above.
  // Finally, abort the retry thread in case it runs, and join it.
  abortRetryCondition.notify_all();
  if (retryThread.joinable()) {
    retryThread.join();
  }
  if (headRetryThread.joinable()) {
    headRetryThread.join();
  }
}

bool StreamingInputStream::HasFatalError() {
  return fatalErrorOccurred;
}

void StreamingInputStream::StreamRange(s64 from, s64 to, bool allowExtendRange, s64 maxStreamSize) {
  if (kDebug) { LOG(1) << "StreamingInputStream: StreamRange() from " << from << " to " << to; }
  if (minStreamSize < 0) { LOG(ERROR) << "The stream must be opened before calling this function"; return; }
  
  if (!WaitForHeadRequest() || fatalErrorOccurred) {
    return;
  }
  
  auto rangesLock = ranges.Lock();
  
  // Intersect the range with the existing ranges.
  // Call StreamRangeImpl() only for the new parts.
  struct PlannedRange {
    s64 from;
    s64 to;
  };
  
  vector<PlannedRange> newRanges = {{from, to}};
  
  auto removeExistingRange = [&newRanges](s64 existingFrom, s64 existingTo) {
    for (int i = 0; i < newRanges.size(); ++ i) {
      auto& newRange = newRanges[i];
      
      if (existingTo < newRange.from) {
        // The old range ends before the new range starts.
        // Since newRanges are ordered (increasing), we can return here as this condition will be true for all following `newRange` entries as well
        return;
      }
      if (existingTo < newRange.to) {
        if (existingFrom <= newRange.from) {
          // The old range overlaps the lower part of the new range.
          newRange.from = existingTo + 1;
        } else {
          // The old range is within the new range.
          // Split newRange into two.
          PlannedRange first = {newRange.from, existingFrom - 1};
          PlannedRange second = {existingTo + 1, newRange.to};
          newRange = first;
          newRanges.insert(newRanges.begin() + (i + 1), second);
        }
        return;
      }
      
      if (existingFrom > newRange.to) {
        // The old range comes after the new range.
        continue;
      }
      if (existingFrom > newRange.from) {
        // The old range overlaps the higher part of the new range.
        newRange.to = existingFrom - 1;
        continue;
      }
      
      // The old range completely covers the new range.
      // Delete newRange.
      newRanges.erase(newRanges.begin() + i);
      -- i;
    }
  };
  
  for (const auto& rangeItem : rangesLock->cachedRanges) {
    const auto& range = rangeItem.range;
    removeExistingRange(range->ContentRangeFrom(), range->ContentRangeTo());
  }
  
  if (rangesLock->currentRange) {
    removeExistingRange(rangesLock->currentScheduledRange.from, rangesLock->currentScheduledRange.to);
  }
  
  for (const auto& range : rangesLock->scheduledRanges) {
    removeExistingRange(range.from, range.to);
  }
  
  for (int rangeIdx = 0, newRangesSize = newRanges.size(); rangeIdx < newRangesSize; ++ rangeIdx) {
    PlannedRange& range = newRanges[rangeIdx];
    
    if (allowExtendRange) {
      // In order to optimize the streaming, extend the range in these two cases:
      // - The range is smaller than minStreamSize
      // - The range would leave gaps on either side that are smaller than minStreamSize
      if (rangeIdx == 0) {
        const s64 minFrom = FindPreviousRangeEnd(range.from, &rangesLock) + 1;
        
        const s64 rangeSize = range.to - range.from + 1;
        if (rangeSize < minStreamSize) {
          range.from = std::max(minFrom, range.from - (minStreamSize - rangeSize));
        }
        
        if (range.from - minFrom < minStreamSize) {
          range.from = minFrom;
        }
      }
      if (rangeIdx == newRangesSize - 1) {
        const s64 maxTo = FindNextRangeStart(range.to, &rangesLock) - 1;
        
        const s64 rangeSize = range.to - range.from + 1;
        if (rangeSize < minStreamSize) {
          range.to = std::min(maxTo, range.to + (minStreamSize - rangeSize));
        }
        
        if (maxTo - range.to < minStreamSize) {
          range.to = maxTo;
        }
      }
    }
    
    if (maxStreamSize > 0) {
      const s64 chunkCount = 1 + (range.to - range.from + 1 - 1) / maxStreamSize;
      
      // Finding the chunk boundaries can be though of as looking at the intermediate places between
      // the from / to numbers, chunking them, and then going to the previous / next number to get chunk ends / starts.
      //
      // Sketch:
      // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4  (from: 4, to: 11)
      // . . . . < - - > < - - > . . .  (chunks: 4 to 7, 8 to 11)
      //  | | | | | | | | | | | | | |   (intermediate)
      const s64 intermediateFrom = range.from - 1;
      const s64 intermediateTo = range.to;
      
      for (s64 chunkIdx = 0; chunkIdx < chunkCount; ++ chunkIdx) {
        const s64 chunkFrom = intermediateFrom + ((intermediateTo - intermediateFrom) * chunkIdx) / chunkCount + 1;
        const s64 chunkTo = intermediateFrom + ((intermediateTo - intermediateFrom) * (chunkIdx + 1)) / chunkCount;
        ScheduleRange(chunkFrom, chunkTo, /*allowExtendRange*/ false, /*bypassQueue*/ false, /*isProtected*/ false, &rangesLock);
      }
    } else {
      ScheduleRange(range.from, range.to, /*allowExtendRange*/ false, /*bypassQueue*/ false, /*isProtected*/ false, &rangesLock);
    }
  }
}

void StreamingInputStream::DropPendingRequests() {
  if (kDebug) { LOG(1) << "StreamingInputStream: DropPendingRequests()"; }
  if (minStreamSize < 0) { LOG(ERROR) << "The stream must be opened before calling this function"; return; }
  
  auto rangesLock = ranges.Lock();
  
  auto& scheduledRanges = rangesLock->scheduledRanges;
  scheduledRanges.erase(std::remove_if(scheduledRanges.begin(), scheduledRanges.end(), [](const ScheduledRange& range) { return !range.isProtected; }), scheduledRanges.end());
}

usize StreamingInputStream::Read(void* data, usize size) {
  // if (kDebug) { LOG(1) << "StreamingInputStream: Read()"; }
  if (minStreamSize < 0) { LOG(ERROR) << "The stream must be opened before calling this function"; return 0; }
  
  if (size == 0 || fatalErrorOccurred) {
    if (kDebug && fatalErrorOccurred) { LOG(1) << "StreamingInputStream: Read() returning 0 since fatalErrorOccurred is true"; }
    return 0;
  }
  
  if (!WaitForHeadRequest()) { return 0; }
  
  if (filePosition >= headRequest->ContentLength()) {
    if (kDebug) { LOG(1) << "StreamingInputStream: Read() returning 0 since filePosition (" << filePosition << ") is beyond the ContentLength (" << headRequest->ContentLength() << ")"; }
    return 0;
  }
  
  // Check which parts of the requested range are:
  // - Already available,
  // - Scheduled for download,
  // - Neither available nor scheduled.
  //
  // Any required scheduled ranges are moved to the start of the queue.
  // Any unavailable and non-scheduled ranges are added in to the start of the queue.
  struct MissingRange {
    s64 rangeFrom;
    s64 rangeTo;
    
    s64 copyFrom;
    usize copySize;
    u8* copyDest;
  };
  vector<MissingRange> missingRanges;
  
  s64 curFilePosition = filePosition;
  usize remainingSize = size;
  
  {
    auto rangesLock = ranges.Lock();
    int cachedRangeIdx = 0;
    
    int rescheduledRangesCount = 0;
    
    // Skip over cached ranges that come before the current part
    while (cachedRangeIdx < rangesLock->cachedRanges.size() &&
            rangesLock->cachedRanges[cachedRangeIdx].range->ContentRangeTo() < curFilePosition) {
      ++ cachedRangeIdx;
    }
    
    while (curFilePosition < headRequest->ContentLength() && remainingSize > 0) {
      u8* copyDest = static_cast<u8*>(data) + curFilePosition - filePosition;
      
      // If we have a cached range that covers the current part, use it
      if (cachedRangeIdx < rangesLock->cachedRanges.size()) {
        auto& curRangeItem = rangesLock->cachedRanges[cachedRangeIdx];
        const auto& curRange = curRangeItem.range;
        
        if (curRange->ContentRangeFrom() <= curFilePosition) {
          ++ accessCounter;
          curRangeItem.lastAccess = accessCounter;
          
          const usize usableSize = std::min<s64>(remainingSize, (curRange->ContentRangeTo() + 1) - curFilePosition);
          memcpy(copyDest, curRange->Content() + curFilePosition - curRange->ContentRangeFrom(), usableSize);
          
          curFilePosition += usableSize;
          remainingSize -= usableSize;
          
          ++ cachedRangeIdx;
          continue;
        }
      }
      
      // We don't have a cached range covering the current part.
      // Check whether it is being downloaded or is scheduled.
      // If yes, re-schedule it to the start of the queue, if not, add it at the start of the queue.
      auto checkScheduledRange = [&missingRanges, &curFilePosition, &remainingSize, copyDest](ScheduledRange& scheduledRange) {
        if (scheduledRange.from > curFilePosition || scheduledRange.to < curFilePosition) {
          return false;
        }
        
        scheduledRange.isProtected = true;
        
        missingRanges.emplace_back();
        auto& missingRange = missingRanges.back();
        missingRange.rangeFrom = scheduledRange.from;
        missingRange.rangeTo = scheduledRange.to;
        missingRange.copyFrom = curFilePosition - missingRange.rangeFrom;
        missingRange.copySize = std::min<s64>(remainingSize, (missingRange.rangeTo + 1) - curFilePosition);
        missingRange.copyDest = copyDest;
        
        curFilePosition += missingRange.copySize;
        remainingSize -= missingRange.copySize;
        
        return true;
      };
      
      if (rangesLock->currentRange && checkScheduledRange(rangesLock->currentScheduledRange)) {
        continue;
      }
      
      bool rangeAlreadyScheduled = false;
      for (int scheduledRangeIdx = 0; scheduledRangeIdx < rangesLock->scheduledRanges.size(); ++ scheduledRangeIdx) {
        if (checkScheduledRange(rangesLock->scheduledRanges[scheduledRangeIdx])) {
          // Re-schedule the range to the start of the queue.
          std::swap(rangesLock->scheduledRanges[scheduledRangeIdx], rangesLock->scheduledRanges[rescheduledRangesCount]);
          ++ rescheduledRangesCount;
          rangeAlreadyScheduled = true;
          break;
        }
      }
      
      if (rangeAlreadyScheduled) {
        continue;
      }
      
      // The current part is neither cached nor scheduled.
      // Request a new range to cover this part.
      const s64 maxTo = FindNextRangeStart(curFilePosition, &rangesLock) - 1;
      ScheduledRange newRange = ScheduleRange(curFilePosition, std::min<s64>(filePosition + size - 1, maxTo), /*allowExtendRange*/ true, /*bypassQueue*/ true, /*protectRange*/ true, &rangesLock);
      
      missingRanges.emplace_back();
      auto& missingRange = missingRanges.back();
      missingRange.rangeFrom = newRange.from;
      missingRange.rangeTo = newRange.to;
      missingRange.copyFrom = curFilePosition - missingRange.rangeFrom;
      missingRange.copySize = std::min<s64>(remainingSize, (missingRange.rangeTo + 1) - curFilePosition);
      missingRange.copyDest = copyDest;
      
      curFilePosition += missingRange.copySize;
      remainingSize -= missingRange.copySize;
    }
    
    if (kDebug && !missingRanges.empty()) { LOG(1) << "StreamingInputStream: Read() has " << missingRanges.size() << " missing ranges, waiting for it / them ..."; }
    
    abortCurrentRead = false;
    
    // Wait for all downloads to complete that were still missing,
    // and use their data after they completed.
    int missingRangeIdx = 0;
    
    while (missingRangeIdx < missingRanges.size()) {
      const auto& missingRange = missingRanges[missingRangeIdx];
      
      // Search for the missing range among the cachedRanges.
      // If not found, wait for the next range to finish downloading (or for a fatal error to occur).
      bool rangeFound = false;
      
      for (auto& cachedRangeItem : rangesLock->cachedRanges) {
        auto& cachedRange = cachedRangeItem.range;
        
        if (cachedRange->ContentRangeFrom() == missingRange.rangeFrom &&
            cachedRange->ContentRangeTo() == missingRange.rangeTo) {
          // Found the range. Use its content and remove its protection.
          ++ accessCounter;
          cachedRangeItem.lastAccess = accessCounter;
          
          memcpy(missingRange.copyDest, cachedRange->Content() + missingRange.copyFrom, missingRange.copySize);
          cachedRangeItem.isProtected = false;
          
          rangeFound = true;
          break;
        }
      }
      
      if (rangeFound) {
        ++ missingRangeIdx;
        continue;
      }
      
      if (rangesLock->currentRange == nullptr && rangesLock->scheduledRanges.empty()) {
        // At least one range is still missing, but no download is in progress anymore.
        // Since we protect all missing ranges from being dropped, this should in theory never happen.
        LOG(ERROR) << "Failed to wait for missing streamed ranges";
        return 0;
      }
      
      // The range we are looking for is not downloaded yet. Wait for the next download to finish, and then retry to find it.
      newRangeCondition.wait(rangesLock.GetLock());
      
      if (abortCurrentRead || fatalErrorOccurred) {
        if (kDebug) {
          if (abortCurrentRead) { LOG(1) << "StreamingInputStream: Read() aborted (abortCurrentRead is true)"; }
          else if (fatalErrorOccurred) { LOG(1) << "StreamingInputStream: Read() aborted (fatalErrorOccurred is true)"; }
        }
        return 0;
      }
    }
    
    if (kDebug && !missingRanges.empty()) { LOG(1) << "StreamingInputStream: Read() got all missing ranges. cachedRanges.size(): " << rangesLock->cachedRanges.size()
                                                   << ", scheduledRanges.size(): " << rangesLock->scheduledRanges.size(); }
  }
  
  // if (kDebug) { LOG(1) << "StreamingInputStream: Read() returning " << (size - remainingSize) << " bytes of data"; }
  
  // Update filePosition
  filePosition += (size - remainingSize);
  return (size - remainingSize);
}

void StreamingInputStream::AbortRead() {
  // Note: This implementation won't abort Read() if Read()'s execution is still before the point where it acquires `rangesLock`.
  //       This should not be a problem though.
  // TODO: Aborting a read may leave protected cached and/or scheduled ranges behind. Their isProtected flag should be removed in that case!
  if (kDebug) { LOG(1) << "StreamingInputStream: AbortRead()"; }
  {
    auto rangesLock = ranges.Lock();
    abortCurrentRead = true;
  }
  newRangeCondition.notify_all();
  headRequestSuccessfulCondition.notify_all();
}

bool StreamingInputStream::Seek(u64 offsetFromStart) {
  if (minStreamSize < 0) { LOG(ERROR) << "The stream must be opened before calling this function"; return false; }
  
  if (!WaitForHeadRequest() || offsetFromStart > headRequest->ContentLength()) { return false; }
  
  filePosition = offsetFromStart;
  return true;
}

u64 StreamingInputStream::SizeInBytes() {
  if (minStreamSize < 0) { LOG(ERROR) << "The stream must be opened before calling this function"; return 0; }
  
  if (!WaitForHeadRequest()) { return 0; }
  
  return headRequest->ContentLength();
}

s64 StreamingInputStream::FindPreviousRangeEnd(s64 position, LockedWrapMutex<Ranges>* rangesLock) {
  LockedWrapMutex<Ranges>& lock = *rangesLock;
  
  s64 result = -1;
  
  // TODO: Use a binary search in lock->cachedRanges
  for (const auto& rangeItem : lock->cachedRanges) {
    const auto& range = rangeItem.range;
    if (range->ContentRangeTo() < position) {
      result = std::max(result, range->ContentRangeTo());
    }
  }
  
  if (lock->currentRange && lock->currentScheduledRange.to < position) {
    result = std::max(result, lock->currentScheduledRange.to);
  }
  
  for (const auto& range : lock->scheduledRanges) {
    if (range.to < position) {
      result = std::max(result, range.to);
    }
  }
  
  return result;
}

s64 StreamingInputStream::FindNextRangeStart(s64 position, LockedWrapMutex<Ranges>* rangesLock) {
  LockedWrapMutex<Ranges>& lock = *rangesLock;
  
  s64 result = headRequest->ContentLength();
  
  // TODO: Use a binary search in lock->cachedRanges
  for (const auto& rangeItem : lock->cachedRanges) {
    const auto& range = rangeItem.range;
    if (range->ContentRangeFrom() > position) {
      result = std::min(result, range->ContentRangeFrom());
    }
  }
  
  if (lock->currentRange && lock->currentScheduledRange.from > position) {
    result = std::min(result, lock->currentScheduledRange.from);
  }
  
  for (const auto& range : lock->scheduledRanges) {
    if (range.from > position) {
      result = std::min(result, range.from);
    }
  }
  
  return result;
}

StreamingInputStream::ScheduledRange StreamingInputStream::ScheduleRange(s64 from, s64 to, bool allowExtendRange, bool bypassQueue, bool protectRange, LockedWrapMutex<Ranges>* rangesLock) {
  if (kDebug) { LOG(1) << "StreamingInputStream: ScheduleRange(), range: " << from << " to " << to << ", allowExtendRange: " << allowExtendRange
                       << ", bypassQueue: " << bypassQueue << ", protectRange: " << protectRange; }
  LockedWrapMutex<Ranges>& lock = *rangesLock;
  
  if (allowExtendRange) {
    // In order to optimize the streaming, extend the range in these two cases:
    // - The range is smaller than minStreamSize
    // - The range would leave gaps on either side that are smaller than minStreamSize
    const s64 minFrom = FindPreviousRangeEnd(from, rangesLock) + 1;
    const s64 maxTo = FindNextRangeStart(to, rangesLock) - 1;
    
    s64 rangeSize = to - from + 1;
    if (rangeSize < minStreamSize) {
      // Try to extend the end first.
      to = std::min(maxTo, to + (minStreamSize - rangeSize));
      rangeSize = to - from + 1;
      
      // If that did not suffice, try to extend the start.
      if (rangeSize < minStreamSize) {
        from = std::max(minFrom, from - (minStreamSize - rangeSize));
      }
    }
    
    if (from - minFrom < minStreamSize) {
      from = minFrom;
    }
    
    if (maxTo - to < minStreamSize) {
      to = maxTo;
    }
  }
  
  ScheduledRange newScheduledRange(from, to, scheduleCounter, protectRange);
  ++ scheduleCounter;
  
  if (lock->currentRange == nullptr) {
    StartDownload(newScheduledRange, rangesLock);
  } else if (bypassQueue) {
    lock->scheduledRanges.insert(lock->scheduledRanges.begin(), newScheduledRange);
  } else {
    lock->scheduledRanges.push_back(newScheduledRange);
  }
  
  return newScheduledRange;
}

bool StreamingInputStream::WaitForHeadRequest() {
  unique_lock<mutex> lock(headRequestSuccessfulMutex);
  while (!headRequestSuccessful && !shuttingDown && !fatalErrorOccurred && !abortCurrentRead) {
    headRequestSuccessfulCondition.wait(lock);
  }
  if (kDebug && !headRequestSuccessful) { LOG(1) << "StreamingInputStream: WaitForHeadRequest() will return false"; }
  return headRequestSuccessful;
}

void StreamingInputStream::StartDownload(const ScheduledRange& range, LockedWrapMutex<Ranges>* rangesLock) {
  if (kDebug) { LOG(1) << "StreamingInputStream: StartDownload(), range: " << range.from << " to " << range.to; }
  LockedWrapMutex<Ranges>& lock = *rangesLock;
  
  if (lock->currentRange) {
    LOG(ERROR) << "A download is already in progress";
    return;
  }
  
  lock->currentScheduledRange = range;
  
  lock->currentRange = httpRequestFactory->CreateHttpRequest();
  lock->currentRange->SetCompletionCallback(&StreamingInputStream::DownloadFinishedOrFailedCallbackStatic, this);
  if (!lock->currentRange->SendRangeRequest(HttpRequest::Verb::GET, uri.c_str(), range.from, range.to, allowUntrustedCertificates)) {
    if (retryThread.joinable()) {
      // TODO: Check whether this wait is fully thread-safe with shutdown and with the retry thread
      retryThread.join();
    }
    retryThread = std::thread(&StreamingInputStream::RetryThreadMain, this);
  }
}

void StreamingInputStream::RetryThreadMain() {
  if (kDebug) { LOG(1) << "StreamingInputStream: RetryThreadMain()"; }
  
  // Wait shortly to prevent creating 100% CPU load in case all retries fail immediately
  // TODO: Would possible spurious wake-ups of wait_until() (which will return std::cv_status::no_timeout) cause issues here?
  {
    unique_lock<mutex> lock(abortRetryMutex);
    auto waitUntil = chrono::steady_clock::now() + 5ms;
    if (abortRetryCondition.wait_until(lock, waitUntil) == std::cv_status::no_timeout) {
      return;
    }
  }
  
  while (true) {
    {
      auto rangesLock = ranges.Lock();
      
      if (rangesLock->currentRange == nullptr) {
        // Cancel() has been called.
        return;
      }
      
      rangesLock->currentRange = httpRequestFactory->CreateHttpRequest();
      rangesLock->currentRange->SetCompletionCallback(&StreamingInputStream::DownloadFinishedOrFailedCallbackStatic, this);
      if (rangesLock->currentRange->SendRangeRequest(HttpRequest::Verb::GET, uri.c_str(), rangesLock->currentScheduledRange.from, rangesLock->currentScheduledRange.to, allowUntrustedCertificates)) {
        return;
      }
    }
    
    this_thread::sleep_for(1ms);
  }
}

void StreamingInputStream::DownloadFinishedOrFailedCallback(HttpRequest* request, bool success) {
  if (kDebug) { LOG(1) << "StreamingInputStream: DownloadFinishedOrFailedCallback(), success: " << success; }
  lock_guard<mutex> callbackLock(callbackMutex);
  if (shuttingDown) { return; }
  
  if (!success) {
    if (kDebug) { LOG(WARNING) << "Streaming of a file range failed, starting retry thread ..."; }
    
    // Schedule a retry after a short delay.
    // This delay on the one hand prevents creating 100% CPU load in case all retries fail immediately.
    // On the other hand, it prevents endless recursion if this failure callback is invoked directly by currentRange->SendRangeRequest().
    if (retryThread.joinable()) {
      // TODO: Check whether this wait is fully thread-safe with shutdown and with the retry thread
      retryThread.join();
    }
    retryThread = std::thread(&StreamingInputStream::RetryThreadMain, this);
    
    return;
  }
  
  {
    auto rangesLock = ranges.Lock();
    
    if (request != rangesLock->currentRange.get()) {
      LOG(ERROR) << "Got a download finished/failed callback for a request which is not current";
      return;
    }
    
    if (request->ContentRangeFrom() != rangesLock->currentScheduledRange.from ||
        request->ContentRangeTo() != rangesLock->currentScheduledRange.to) {
      LOG(ERROR) << "Got a different content range (" << request->ContentRangeFrom() << " to " << request->ContentRangeTo()
                << ") from the server than requested (" << rangesLock->currentScheduledRange.from << " to " << rangesLock->currentScheduledRange.to
                << "). Possibly the file was truncated on the server after streaming started? We likely cannot continue streaming in this situation, giving up.";
      fatalErrorOccurred = true;
      rangesLock.GetLock().unlock();
      newRangeCondition.notify_all();  // Un-block Read() in case it was waiting for this range
      return;
    }
    
    // Move currentRange into its correct place in cachedRanges.
    // TODO: Use a binary search for that
    int newScheduledRangeIdx = -1;
    
    for (int i = 0; i < rangesLock->cachedRanges.size(); ++ i) {
      if (rangesLock->currentScheduledRange.to < rangesLock->cachedRanges[i].range->ContentRangeFrom()) {
        rangesLock->cachedRanges.emplace(
            rangesLock->cachedRanges.begin() + i,
            std::move(rangesLock->currentRange),
            rangesLock->currentScheduledRange.scheduleCounter,
            rangesLock->currentScheduledRange.isProtected);
        newScheduledRangeIdx = i;
        break;
      }
    }
    
    if (newScheduledRangeIdx < 0) {
      newScheduledRangeIdx = rangesLock->cachedRanges.size();
      rangesLock->cachedRanges.emplace_back(
          std::move(rangesLock->currentRange),
          rangesLock->currentScheduledRange.scheduleCounter,
          rangesLock->currentScheduledRange.isProtected);
    }
    
    rangesLock->currentRange = nullptr;
    
    // If the cached ranges exceed maxCacheSize, remove ranges from the cache as needed.
    // Removal heuristic:
    // - Do not remove any range that is required for a Read() operation that is in progress.
    // - Do not remove the range that was last accessed by a read operation, as it is very likely to be read again.
    // - Do not remove that has just been inserted (this could cause threading issues where a thread
    //   attempts to wait for itself to complete).
    // - Do not remove any range that has been scheduled after the range that was last accessed by a read operation.
    //   This allows to pre-schedule ranges without the chance of them being discarded again before they will be needed.
    //   (It however also allows to exceed maxCacheSize by arbitrary amounts.)
    // - From the remaining ranges, remove the range(s) that have been accessed by a read operation the longest ago.
    int cleanedRangesCount = 0;
    usize cleanedUpBytes = 0;
    
    const CachedRange* lastUsedRange = nullptr;
    usize cacheSize = 0;
    
    for (const auto& rangeItem : rangesLock->cachedRanges) {
      const auto& range = rangeItem.range;
      if (rangeItem.lastAccess == accessCounter) {
        if (lastUsedRange != nullptr) { LOG(ERROR) << "Found multiple ranges having the current accessCounter value"; }
        lastUsedRange = &rangeItem;
      }
      cacheSize += range->ContentRangeTo() - range->ContentRangeFrom() + 1;
    }
    
    if (cacheSize > maxCacheSize) {
      vector<int> removableItems;
      removableItems.reserve(rangesLock->scheduledRanges.size());
      
      for (int rangeItemIdx = 0; rangeItemIdx < rangesLock->cachedRanges.size(); ++ rangeItemIdx) {
        const auto& rangeItem = rangesLock->cachedRanges[rangeItemIdx];
        
        if (rangeItemIdx != newScheduledRangeIdx &&
            &rangeItem != lastUsedRange &&
            !rangeItem.isProtected &&
            (lastUsedRange == nullptr || rangeItem.scheduleCounter < lastUsedRange->scheduleCounter)) {
          removableItems.emplace_back(rangeItemIdx);
        }
      }
      
      std::sort(removableItems.begin(), removableItems.end(), [&rangesLock](int a, int b) {
        const auto& aRange = rangesLock->cachedRanges[a];
        const auto& bRange = rangesLock->cachedRanges[b];
        
        return aRange.lastAccess < bRange.lastAccess;
      });
      
      int cleanupEndIndex = 0;
      while (cleanupEndIndex < removableItems.size() && cacheSize > maxCacheSize) {
        const auto& cleanedRange = rangesLock->cachedRanges[removableItems[cleanupEndIndex]].range;
        const s64 rangeSize = cleanedRange->ContentRangeTo() - cleanedRange->ContentRangeFrom() + 1;
        
        cacheSize -= rangeSize;
        
        cleanedUpBytes += rangeSize;
        ++ cleanedRangesCount;
        
        ++ cleanupEndIndex;
      }
      removableItems.resize(cleanupEndIndex);
      
      std::sort(removableItems.begin(), removableItems.end());
      
      if (!removableItems.empty()) {
        int sourceIdx = removableItems.front() + 1;
        int destIdx = removableItems.front();
        auto nextRemovableItemIt = removableItems.begin() + 1;
        
        while (true) {
          while (nextRemovableItemIt != removableItems.end() && *nextRemovableItemIt == sourceIdx) {
            ++ sourceIdx;
            ++ nextRemovableItemIt;
          }
          
          if (sourceIdx >= rangesLock->cachedRanges.size()) {
            break;
          }
          
          rangesLock->cachedRanges[destIdx] = std::move(rangesLock->cachedRanges[sourceIdx]);
          
          ++ destIdx;
          ++ sourceIdx;
        }
        
        rangesLock->cachedRanges.resize(rangesLock->cachedRanges.size() - removableItems.size());
      }
    }
    
    // Start the next download (if any is queued)
    if (!rangesLock->scheduledRanges.empty()) {
      ScheduledRange range = rangesLock->scheduledRanges.front();
      rangesLock->scheduledRanges.erase(rangesLock->scheduledRanges.begin());
      
      StartDownload(range, &rangesLock);
    }
    
    // Debug logging
    if (kDebugLogStatistics) {
      usize cachedBytes = 0;
      for (const auto& rangeItem : rangesLock->cachedRanges) {
        const auto& range = rangeItem.range;
        cachedBytes += range->ContentRangeTo() - range->ContentRangeFrom() + 1;
      }
      
      usize scheduledBytes = 0;
      for (const auto& range : rangesLock->scheduledRanges) {
        scheduledBytes += range.to - range.from + 1;
      }
      
      LOG(1) << "Streaming stats: cached: " << rangesLock->cachedRanges.size() << " (" << 0.1 * ((cachedBytes + (1024 * 1024 / 10) / 2) / (1024 * 1024 / 10)) << " MiB) |"
                " cleaned: " << cleanedRangesCount << " (" << 0.1 * ((cleanedUpBytes + (1024 * 1024 / 10) / 2) / (1024 * 1024 / 10)) << " MiB) |"
                " scheduled: " << rangesLock->scheduledRanges.size() << " (" << 0.1 * ((scheduledBytes + (1024 * 1024 / 10) / 2) / (1024 * 1024 / 10)) << " MiB) |"
                " in_progress: " << ((rangesLock->currentRange != nullptr) ? 1 : 0);
    }
  }
  
  // If the new range was needed for an ongoing Read() call, notify it
  newRangeCondition.notify_all();
}

void StreamingInputStream::DownloadFinishedOrFailedCallbackStatic(HttpRequest* request, bool success, void* userPtr) {
  StreamingInputStream* self = reinterpret_cast<StreamingInputStream*>(userPtr);
  self->DownloadFinishedOrFailedCallback(request, success);
}

bool StreamingInputStream::StartHeadRequest() {
  if (kDebug) { LOG(1) << "StreamingInputStream: StartHeadRequest()"; }
  
  headRequest = httpRequestFactory->CreateHttpRequest();
  headRequest->SetCompletionCallback(&StreamingInputStream::HeadFinishedOrFailedCallbackStatic, this);
  return headRequest->Send(HttpRequest::Verb::HEAD, uri.c_str(), allowUntrustedCertificates);
}

void StreamingInputStream::HeadRetryThreadMain() {
  if (kDebug) { LOG(1) << "StreamingInputStream: HeadRetryThreadMain()"; }
  
  // Wait shortly to prevent creating 100% CPU load in case all retries fail immediately
  // TODO: Would possible spurious wake-ups of wait_until() (which will return std::cv_status::no_timeout) cause issues here?
  {
    unique_lock<mutex> lock(abortRetryMutex);
    auto waitUntil = chrono::steady_clock::now() + 5ms;
    if (abortRetryCondition.wait_until(lock, waitUntil) == std::cv_status::no_timeout) {
      return;
    }
  }
  
  while (true) {
    {
      // Note: Holding this mutex here helps shutting down cleanly.
      auto rangesLock = ranges.Lock();
      if (shuttingDown) { return; }
      
      if (StartHeadRequest()) { return; }
    }
    
    this_thread::sleep_for(1ms);
  }
}

void StreamingInputStream::HeadFinishedOrFailedCallback(HttpRequest* /*request*/, bool success) {
  if (kDebug) {
    LOG(1) << "StreamingInputStream: HeadFinishedOrFailedCallback(), success: " << success;
    if (success) {
      LOG(1) << "StreamingInputStream: HEAD request Content-Length is: " << headRequest->ContentLength();
    }
  }
  
  lock_guard<mutex> callbackLock(callbackMutex);
  if (shuttingDown) { return; }
  
  if (success) {
    headRequestSuccessfulMutex.lock();
    headRequestSuccessful = true;
    headRequestSuccessfulMutex.unlock();
    headRequestSuccessfulCondition.notify_all();
  } else {
    if (kDebug) { LOG(WARNING) << "Streaming connection failed, retrying ..."; }
    
    // Schedule a retry after a short delay.
    // This delay on the one hand prevents creating 100% CPU load in case all retries fail immediately.
    // On the other hand, it prevents endless recursion if this failure callback is invoked directly by the Send() call.
    if (headRetryThread.joinable()) {
      // TODO: Check whether this wait is fully thread-safe with shutdown and with the retry thread
      headRetryThread.join();
    }
    headRetryThread = std::thread(&StreamingInputStream::HeadRetryThreadMain, this);
  }
}

void StreamingInputStream::HeadFinishedOrFailedCallbackStatic(HttpRequest* request, bool success, void* userPtr) {
  StreamingInputStream* self = reinterpret_cast<StreamingInputStream*>(userPtr);
  self->HeadFinishedOrFailedCallback(request, success);
}

}
