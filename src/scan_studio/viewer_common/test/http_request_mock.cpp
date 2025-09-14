#include "scan_studio/viewer_common/test/http_request_mock.hpp"

namespace scan_studio {

MockHttpRequest::~MockHttpRequest() {
  Abort();
}

bool MockHttpRequest::SendRangeRequest(HttpRequest::Verb verb, const char* /*uri*/, s64 rangeFrom, s64 rangeTo, bool /*allowUntrustedCertificates*/) {
  Abort();
  if (rangeFrom > rangeTo) {
    LOG(FATAL) << "Invalid range specified: " << rangeFrom << " to " << rangeTo;
    return false;
  }
  requestThread = std::thread(&MockHttpRequest::RequestThreadMain, this, verb, rangeFrom, rangeTo);
  return true;
}

void MockHttpRequest::Abort() {
  // For simplicity, we don't support aborting here in this test.
  // Instead, we just wait until the request thread completes
  // (ensuring that no callbacks will be called anymore after Abort() returns).
  if (requestThread.joinable()) {
    requestThread.join();
  }
}

const u8* MockHttpRequest::Content() {
  if (!headersCompleteOrFailed) { LOG(ERROR) << "Content() accessed when headers were not complete yet"; }
  return content->data() + contentRangeFrom;
}

void MockHttpRequest::RequestThreadMain(HttpRequest::Verb verb, s64 rangeFrom, s64 rangeTo) {
  // Clamp the range to the file size (if specified)
  if (rangeFrom >= 0 && rangeTo >= 0) {
    rangeFrom = std::min<s64>(rangeFrom, content->size() - 1);
    rangeTo = std::min<s64>(rangeTo, content->size() - 1);
  }
  
  // Simulate receiving the headers
  statusCode = 200;
  if (rangeFrom < 0 || rangeTo < 0) {
    contentLength = content->size();
  } else {
    contentLength = rangeTo - rangeFrom + 1;
  }
  contentRangeFrom = rangeFrom;
  contentRangeTo = rangeTo;
  
  headersCompleteOrFailedMutex.lock();
  headersCompleteOrFailed = true;
  headersCompleteOrFailedMutex.unlock();
  headersCompleteOrFailedCondition.notify_all();
  
  if (statusCode < 0) {
    // The request failed.
    if (completionCallback) { completionCallback(this, /*success*/ false, completionCallbackUserPtr); }
    return;
  } else if (verb == HttpRequest::Verb::HEAD) {
    // No content will follow; the request completed successfully.
    if (completionCallback) { completionCallback(this, /*success*/ true, completionCallbackUserPtr); }
  }
  
  // Simulate receiving the content
  actualContentLength = contentLength;
  
  contentCompleteOrFailedMutex.lock();
  contentCompleteOrFailed = true;
  contentCompleteOrFailedMutex.unlock();
  contentCompleteOrFailedCondition.notify_all();
  
  if (completionCallback) { completionCallback(this, /*success*/ actualContentLength >= 0, completionCallbackUserPtr); }
}

}
