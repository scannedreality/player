#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <libvis/vulkan/libvis.h>

#include <loguru.hpp>

namespace scan_studio {
using namespace vis;

class HttpRequest;

/// Callback for finished or failed requests
typedef void (* RequestFinishedOrFailedCallback)(HttpRequest* request, bool success, void* userPtr);

class HttpRequestFactory {
 public:
  virtual ~HttpRequestFactory() {}
  virtual unique_ptr<HttpRequest> CreateHttpRequest() = 0;
};

/// Base class for HTTP requests to be used by StreamingInputStream.
class HttpRequest {
 public:
  /// HTTP verbs that may be used with this class.
  /// Important: The values must correspond to those used by implementations that employ other languages, such as C# for the Unity plugin.
  enum class Verb {
    HEAD = 0,
    GET = 1
  };
  
  inline HttpRequest() {}
  
  HttpRequest(const HttpRequest& other) = delete;
  HttpRequest& operator= (const HttpRequest& other) = delete;
  
  HttpRequest(HttpRequest&& other) = delete;
  HttpRequest& operator= (HttpRequest&& other) = delete;
  
  /// Aborts the request in case it is running.
  /// Notice that this waits for the completion callback to finish in case it is running.
  virtual ~HttpRequest() {};
  
  /// Sets a callback to be executed when the request completes (succeeds or fails).
  /// Must be called before calling any variant of Send().
  /// Note: The callback is not called if the request is aborted.
  /// Note: Do not make any assumptions about which thread will call the completion callback.
  ///       It may be called by different threads depending on whether the request succeeds or fails.
  ///       It may even be directly called by calls to Send() or SendRangeRequest() if a request fails immediately.
  inline void SetCompletionCallback(RequestFinishedOrFailedCallback callback, void* userPtr) {
    completionCallback = callback;
    completionCallbackUserPtr = userPtr;
  }
  
  /// Sends a request with the given HTTP verb to the given URI.
  /// If there is an existing request in progress on this instance of HttpRequest, then that old request is aborted.
  /// Returns true on success, false if there is an error sending the request.
  inline bool Send(Verb verb, const char* uri, bool allowUntrustedCertificates) {
    return SendRangeRequest(verb, uri, -1, -1, allowUntrustedCertificates);
  }
  
  /// Variant of Send(), making an HTTP range request. See:
  /// https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests
  /// rangeFrom and rangeTo must specify a valid, positive byte range.
  virtual bool SendRangeRequest(Verb verb, const char* uri, s64 rangeFrom, s64 rangeTo, bool allowUntrustedCertificates) = 0;
  
  /// Aborts the request in case it is running.
  virtual void Abort() = 0;
  
  
  /// Polls for whether the response headers have been received, or the request has failed.
  inline bool HasCompletedHeaders() { return headersCompleteOrFailed; }
  
  /// Waits for the response headers to be available (or for the request to fail).
  inline void WaitForHeaders() {
    if (headersCompleteOrFailed) { return; }
    
    unique_lock<mutex> lock(headersCompleteOrFailedMutex);
    while (!headersCompleteOrFailed) {
      headersCompleteOrFailedCondition.wait(lock);
    }
  }
  
  
  /// Polls for whether the response content has been received fully, or the request has failed.
  inline bool HasCompletedContent() { return contentCompleteOrFailed; }
  
  /// Waits for the response content to be fully available (or for the request to fail).
  inline void WaitForContent() {
    if (contentCompleteOrFailed) { return; }
    
    unique_lock<mutex> lock(contentCompleteOrFailedMutex);
    while (!contentCompleteOrFailed) {
      contentCompleteOrFailedCondition.wait(lock);
    }
  }
  
  
  /// Returns whether the request succeeded (i.e., a response arrived which has an HTTP status code in the 2xx range).
  inline bool Succeeded() {
    return headersCompleteOrFailed && statusCode >= 200 && statusCode < 300;
  }
  
  /// Returns the received HTTP status code, or -1 in case of an error.
  /// Must only be called once HasCompletedHeaders() returns true or WaitForHeaders() was called.
  inline s32 StatusCode() {
    if (!headersCompleteOrFailed) { LOG(ERROR) << "StatusCode() accessed when headers were not complete yet"; }
    return statusCode;
  }
  
  /// Returns the size of the returned content in bytes (according to the Content-Length HTTP header of the response).
  /// Notice that the actual received content size might be smaller, e.g., if the connection drops while transferring the content.
  /// Must only be called once HasCompletedHeaders() returns true or WaitForHeaders() was called.
  inline s64 ContentLength() {
    if (!headersCompleteOrFailed) { LOG(ERROR) << "ContentLength() accessed when headers were not complete yet"; }
    return contentLength;
  }
  
  /// Returns the index of the first returned content byte (from the Content-Range HTTP header of the response).
  /// Must only be called once HasCompletedHeaders() returns true or WaitForHeaders() was called.
  inline s64 ContentRangeFrom() {
    if (!headersCompleteOrFailed) { LOG(ERROR) << "ContentRangeFrom() accessed when headers were not complete yet"; }
    return contentRangeFrom;
  }
  
  /// Returns the index of the last returned content byte (from the Content-Range HTTP header of the response).
  /// Must only be called once HasCompletedHeaders() returns true or WaitForHeaders() was called.
  inline s64 ContentRangeTo() {
    if (!headersCompleteOrFailed) { LOG(ERROR) << "ContentRangeTo() accessed when headers were not complete yet"; }
    return contentRangeTo;
  }
  
  /// Returns a pointer to the response content.
  /// Must only be called once HasCompletedHeaders() returns true or WaitForHeaders() was called;
  /// in addition, the content itself is only valid once HasCompletedContent() returns true or WaitForContent() was called.
  virtual const u8* Content() = 0;
  
  /// Returns the size of the received content in bytes.
  /// Compare this to ContentLength() to see whether the whole content that was promised by the header was actually received.
  /// Must only be called once HasCompletedContent() returns true or WaitForContent() was called.
  inline s64 ActualContentLength() {
    if (!contentCompleteOrFailed) { LOG(ERROR) << "ActualContentLength() accessed when content was not complete yet"; }
    return actualContentLength;
  }
  
 protected:
  /// If this is true, the values of contentLength, contentRangeFrom, and contentRangeTo are valid.
  /// In case of failure (which includes getting an HTTP response with an error status code), contentLength remains -1.
  atomic<bool> headersCompleteOrFailed = false;
  
  /// These enable waiting for headersCompleteOrFailed to become true.
  mutex headersCompleteOrFailedMutex;
  condition_variable headersCompleteOrFailedCondition;
  
  /// Received HTTP status code.
  s32 statusCode = -1;
  
  /// Value of Content-Length HTTP header.
  /// This determines the *maximum* size of the content, but the actual available content size might be smaller
  /// (in case the header is incorrect, the connection dropped while transferring the content, etc.)
  s64 contentLength = -1;
  
  /// Values of Content-Range HTTP header.
  s64 contentRangeFrom = -1;
  s64 contentRangeTo = -1;
  
  /// If this is true, the values at contentPtr and of actualContentLength are valid.
  /// In case of failure, actualContentLength remains -1.
  atomic<bool> contentCompleteOrFailed = false;
  
  /// These enable waiting for contentCompleteOrFailed to become true.
  mutex contentCompleteOrFailedMutex;
  condition_variable contentCompleteOrFailedCondition;
  
  /// The actual length of the data at contentPtr, which may be smaller than contentLength.
  s64 actualContentLength = -1;
  
  /// A callback to be called when the request completes (succeeds or fails).
  RequestFinishedOrFailedCallback completionCallback = nullptr;
  void* completionCallbackUserPtr;
};

}
