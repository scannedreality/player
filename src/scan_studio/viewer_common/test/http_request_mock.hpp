#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/http_request.hpp"

namespace scan_studio {
using namespace vis;

/// Mock HTTP requests to allow for testing StreamingInputStream.
class MockHttpRequest : public HttpRequest {
 public:
  inline MockHttpRequest(const vector<u8>* content)
      : content(content) {}
  
  virtual ~MockHttpRequest();
  
  virtual bool SendRangeRequest(Verb verb, const char* uri, s64 rangeFrom, s64 rangeTo, bool allowUntrustedCertificates) override;
  virtual void Abort() override;
  virtual const u8* Content() override;
  
 private:
  void RequestThreadMain(HttpRequest::Verb verb, s64 rangeFrom, s64 rangeTo);
  
  std::thread requestThread;
  
  const vector<u8>* content;
};

class MockHttpRequestFactory : public HttpRequestFactory {
 public:
  inline MockHttpRequestFactory(const vector<u8>* content)
      : content(content) {}
  
  virtual ~MockHttpRequestFactory() {}
  
  virtual inline unique_ptr<HttpRequest> CreateHttpRequest() override {
    return unique_ptr<HttpRequest>(new MockHttpRequest(content));
  }
  
 private:
  const vector<u8>* content;
};

}
