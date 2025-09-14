#include "scan_studio/viewer_common/streaming_input_stream.hpp"

#include <gtest/gtest.h>

#include <loguru.hpp>

#include "scan_studio/viewer_common/test/http_request_mock.hpp"

using namespace scan_studio;

static void TestRead(StreamingInputStream* stream, const vector<u8>& mockFile, int readStart, int readLength) {
  ASSERT_TRUE(stream->Seek(readStart));
  
  vector<u8> readResult(readLength);
  ASSERT_EQ(readLength, stream->Read(readResult.data(), readLength));
  
  for (int i = 0; i < readLength; ++ i) {
    EXPECT_EQ(mockFile[readStart + i], readResult[i]);
  }
}

TEST(StreamingInputStream, SimpleRead) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 10,
      /*maxCacheSize*/ 100,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  TestRead(&stream, mockFile, 0, mockFile.size());
}

TEST(StreamingInputStream, TwoReadsForward) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 100,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  TestRead(&stream, mockFile, 0, mockFile.size() / 2);
  TestRead(&stream, mockFile, mockFile.size() / 2, mockFile.size() - mockFile.size() / 2);
}

TEST(StreamingInputStream, TwoReadsBackward) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 100,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  TestRead(&stream, mockFile, mockFile.size() / 2, mockFile.size() - mockFile.size() / 2);
  TestRead(&stream, mockFile, 0, mockFile.size() / 2);
}

TEST(StreamingInputStream, MinStreamSize) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 999,
      /*maxCacheSize*/ 100,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  TestRead(&stream, mockFile, 0, mockFile.size() / 2);
  TestRead(&stream, mockFile, mockFile.size() / 2, mockFile.size() - mockFile.size() / 2);
}

TEST(StreamingInputStream, ExplicitScheduling) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 100,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  stream.StreamRange(/*from*/ 1, /*to*/ 1, /*allowExtendRange*/ false, /*maxStreamSize*/ -1);
  stream.StreamRange(/*from*/ 3, /*to*/ 3, /*allowExtendRange*/ false, /*maxStreamSize*/ -1);
  stream.StreamRange(/*from*/ 0, /*to*/ 30, /*allowExtendRange*/ false, /*maxStreamSize*/ -1);
  
  TestRead(&stream, mockFile, 0, mockFile.size());
}

TEST(StreamingInputStream, CacheCleanup) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 2,  // using a very small maxCacheSize here
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  for (int i = 0; i < mockFile.size(); ++ i) {
    TestRead(&stream, mockFile, i, 1);
  }
}

TEST(StreamingInputStream, RandomReadTest) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 12,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  constexpr int readCount = 256;
  
  for (int i = 0; i < readCount; ++ i) {
    const int a = rand() % mockFile.size();
    const int b = rand() % mockFile.size();
    
    const int readStart = std::min(a, b);
    const int readSize = std::max(a, b) - readStart + 1;
    
    TestRead(&stream, mockFile, readStart, readSize);
  }
}

TEST(StreamingInputStream, RandomReadAndSchedulingTest) {
  srand(time(nullptr));
  
  vector<u8> mockFile(32);
  for (int i = 0; i < mockFile.size(); ++ i) {
    mockFile[i] = rand() % 256;
  }
  
  StreamingInputStream stream;
  stream.Open(
      "test://dummy",
      /*minStreamSize*/ 1,
      /*maxCacheSize*/ 12,
      /*allowUntrustedCertificates*/ true,
      unique_ptr<HttpRequestFactory>(new MockHttpRequestFactory(&mockFile)));
  
  constexpr int iterationCount = 256;
  
  for (int i = 0; i < iterationCount; ++ i) {
    {
      const int a = rand() % mockFile.size();
      const int b = rand() % mockFile.size();
      
      const int readStart = std::min(a, b);
      const int readSize = std::max(a, b) - readStart + 1;
      
      TestRead(&stream, mockFile, readStart, readSize);
    }
    
    {
      const int a = rand() % mockFile.size();
      const int b = rand() % mockFile.size();
      
      const int streamFrom = std::min(a, b);
      const int streamTo = std::max(a, b);
      const bool allowExtendRange = ((rand() % 2) == 0);
      const int maxStreamSize = rand() % mockFile.size();
      
      stream.StreamRange(streamFrom, streamTo, allowExtendRange, maxStreamSize);
    }
  }
}
