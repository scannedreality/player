#ifdef __EMSCRIPTEN__

#include "scan_studio/viewer_common/xrvideo/frame_loading_webcodecs.hpp"

#include <thread>

#include <emscripten.h>
#include <emscripten/wasm_worker.h>

#include <dav1d/dav1d.h>

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"

constexpr bool kVerbose = false;


extern "C" {

EMSCRIPTEN_KEEPALIVE
void WebCodecsWorker_NotifyDecodingThread(int success, int webCodecsContextIntPtr) {
  if (kVerbose) {
    LOG(INFO) << "WebCodecsWorker_NotifyDecodingThread(success: " << success << ")";
  }
  
  auto* webCodecsContext = reinterpret_cast<scan_studio::XRVideoDecodingContext_WebCodecs*>(webCodecsContextIntPtr);
  webCodecsContext->NotifyDecodingThread(success != 0);
}

}


namespace scan_studio {

atomic<int> nextDecoderNumberForEmscripten = 0;

bool XRVideoDecodingContext_WebCodecs::TryInitializeDecoder(const u8* chunkWithHeaderPtr, usize compressedSize, u32 textureWidth, u32 textureHeight) {
  if (kVerbose) {
    LOG(INFO) << "TryInitializeDecoder() ...";
  }
  
  initializationState = WebCodecsState::Failed;
  
  // Get a decoder number that is unique for the current browser page
  decoderNumber = nextDecoderNumberForEmscripten.fetch_add(1);
  
  // Parse the AV.1 sequence header to get the parameters for building the codec string for using WebCodecs
  Dav1dSequenceHeader seqHeader;
  const int headerParseResult = dav1d_parse_sequence_header(&seqHeader, chunkWithHeaderPtr, compressedSize);
  if (headerParseResult == DAV1D_ERR(ENOENT)) {
    LOG(ERROR) << "No sequence header was found in the passed buffer";
    return false;
  } else if (headerParseResult != 0) {
    LOG(ERROR) << "Error trying to parse the sequence header";
    return false;
  }
  
  // The level is given in the format: X.Y, where X (major_level) is equal to 2 + (seq_level_idx >> 2),
  // and Y (minor_level) is given by (seq_level_idx & 3).
  const int level = ((seqHeader.operating_points[0].major_level - 2) << 2) | seqHeader.operating_points[0].minor_level;
  
  // Compile the codec string.
  // For the format, see: https://aomediacodec.github.io/av1-isobmff/#codecsparam
  ostringstream codecStringStream;
  codecStringStream << "av01." << seqHeader.profile << ".";
  codecStringStream.width(2);
  codecStringStream.fill('0');
  codecStringStream << level;
  codecStringStream << ((seqHeader.operating_points[0].tier == 0) ? "M" : "H");
  codecStringStream << ".";
  if (seqHeader.hbd == 0) {
    codecStringStream << "08";
  } else if (seqHeader.hbd == 1) {
    codecStringStream << "10";
  } else {
    codecStringStream << "12";
  }
  codecString = codecStringStream.str();
  const char* codecStringCStr = codecString.c_str();
  if (kVerbose) {
    LOG(INFO) << "codecString: " << codecString;
  }
  
  // Check whether the browser supports WebCodecs at all, and if so, whether our desired decoding config is supported.
  //
  // Note that since we have to wait for the VideoDecoder.isConfigSupported() Promise (i.e., callback) here,
  // and in JavaScript that can only happen when returning to the event loop,
  // and we cannot easily return to the current pthread's event loop in emscripten
  // (unless using emscripten's Asyncify feature, which may significantly increase code size),
  // we proxy this to the main thread as a workaround, assuming that the main thread won't block
  // and thus get to its event loop eventually, allowing the callback to be called.
  //
  // I tried to use a separate pthread or web worker for this, which for some reason did not work,
  // as seemingly no JavaScript callbacks were received in these threads (in the case of a web worker,
  // no code was executed on it at all). Perhaps it had to do with this Chrome bug:
  //   https://bugs.chromium.org/p/chromium/issues/detail?id=1075645
  //
  // Another issue I noticed later is that emscripten had created another output file having the
  // extension ".ww.js" for the web worker attempts. But since I had automated the copying of the
  // output files that I was aware of previously to the local webserver for testing, that file was not copied ...
  // (however, I also didn't see any error log message about this file not being found, so I guess the omission
  //  of that file was not the only problem.)
  //
  // If you want to retry this, you can find the attempts in the git repository history.
  // Note that (at the time of writing), using emscripten's WASM Worker API seems to be incompatible
  // with the PROXY_TO_PTHREAD option that we use for the 'app' version of the web viewer.
  // Also note that emscripten in principle has convenient functions for proxying to other pthreads here:
  //   https://emscripten.org/docs/api_reference/proxying.h.html
  // (but the WebCodecs decoder output callback wasn't called for this pthread if we used the "looper"
  //  variant from the demo below, and nothing was called at all if we used the "returner" variant from
  //  the demo below, likely due to the above Chome bug:
  //    https://github.com/emscripten-core/emscripten/blob/main/test/pthread/test_pthread_proxying_cpp.cpp )
  //
  // Note that since VideoFrame.copyTo() is async, using the main thread should not lead to issues with blocking it too much.
  // However, we will get worse decoding performance, as we will have to wait from time to time until the main thread becomes free for running the decoding-related callbacks.
  const int result = MAIN_THREAD_EM_ASM_INT(({
    if (typeof(VideoDecoder) == 'undefined') {
      return 1;
    }
    
    const config = {
        codec: UTF8ToString($0),
        codedWidth: $1,
        codedHeight: $2,
        optimizeForLatency: true,  // if set to false, the decoder may not output a frame before we inserted multiple frames; but in principle, it could be very helpful for performance ...
    };
    VideoDecoder.isConfigSupported(config)
        .then((supported) => {
          globalThis['xrVideoDecoderSupported' + $3] = supported;
        });
    
    return 0;
  }), codecStringCStr, textureWidth, textureHeight, decoderNumber);
  if (result != 0) {
    if (kVerbose) {
      LOG(INFO) << "WebCodecs API is not supported: VideoDecoder is not defined";
    }
    return false;
  }
  
  // Wait until the .then() callback has executed on the main thread, polling for its result.
  // TODO: As a minor improvement, we can probably make the main thread execute a C++ function in the callback above that
  //       signals a condition variable and gives us the result directly, allowing to use passive waiting instead of polling.
  while (true) {
    const int pollResult = MAIN_THREAD_EM_ASM_INT(({
      var supported = globalThis['xrVideoDecoderSupported' + $0];
      if (typeof(supported) == 'undefined') { return 0; }
      else if (supported === false) { return 1; }
      return 2;
    }), decoderNumber);
    
    if (pollResult == 1) {
      // The callback reported a failure
      if (kVerbose) {
        LOG(INFO) << "WebCodecs: VideoDecoder config is not supported.";
      }
      return false;
    } else if (pollResult == 2) {
      // The callback ran successfully
      break;
    }
    
    // The callback did not execute yet
    this_thread::sleep_for(1ms);
  }
  
  // Our desired decoding config is supported. Create the decoder.
  const int decoderNumber = this->decoderNumber;
  const int webCodecsContextIntPtr = reinterpret_cast<int>(this);
  
  MAIN_THREAD_EM_ASM(({
    const config = {
        codec: UTF8ToString($0),
        codedWidth: $1,
        codedHeight: $2,
        optimizeForLatency: true,  // if set to false, the decoder may not output a frame before we inserted multiple frames; but in principle, it could be very helpful for performance ...
    };
    
    const decoder = new VideoDecoder({
      output: (frame) => {
        // Note that the WebCodecs documentation (https://web.dev/webcodecs/#decoding) advises to return from this callback as fast as possible.
        // So we just push another callback to the event queue using setTimeout() with a timeout of zero,
        // which will be called as soon as control returns to this web worker's event loop.
        // (Not sure whether that makes sense; it only makes sense if the first callback is somehow handled
        //  in a special way, making the decoder wait for it.)
        
        // console.log('WebCodecs: New frame arrived with format: ' + frame.format);
        // globalThis['xrVideoFrames' + $3].push(frame);
        
        setTimeout((frame) => {
          var frameDests = globalThis['xrVideoFrameDest' + $3];
          
          // Assert that the frame has the expected format and size
          if (frame.format != "I420" || frame.allocationSize() != ($1 * $2 * 3) / 2) {
            console.error('WebCodecs: Frame has unexpected format: ' + frame.format + ' or size: ' + frame.allocationSize());
            frame.close();
            frameDests.shift();  // removes the first array element
            _WebCodecsWorker_NotifyDecodingThread(/*success*/ 0, $4);
            return;
          }
          
          // Copy the frame buffer to the destination.
          // TODO: If we use WebGL, we should probably use the frame as an argument to webGl.texImage2D() instead of doing this copy,
          //       which allows it to stay on the GPU in case it was decoded with a GPU-based decoder,
          //       rather than copying it to the CPU first and then transferring it back to the GPU.
          //       However, that would complicate our code, as we would have to pass the resulting texture ID to the
          //       OpenGLXRVideoFrame then somehow, and we would have to maintain a separate XRVideo shader that operates on RGB
          //       textures (instead of YUV textures).
          if (frameDests.length == 0) {
            console.error('WebCodecs: No destination for frame');
            frame.close();
            _WebCodecsWorker_NotifyDecodingThread(/*success*/ 0, $4);
            return;
          }
          const frameDest = frameDests[0];
          frameDests.shift();  // removes the first array element
          frame.copyTo(new Uint8Array(HEAPU8.buffer, HEAPU8.byteOffset + frameDest, frame.allocationSize()))
              .then((layout) => {
                // The frame copy has completed, notify the decoding thread.
                _WebCodecsWorker_NotifyDecodingThread(/*success*/ 1, $4);
                
                // Release the frame
                frame.close();
              });
        }, 0, frame);
      },
      error: (e) => {
        console.error(e.message);
      },
    });
    
    decoder.configure(config);
    
    globalThis['xrVideoDecoder' + $3] = decoder;
    // globalThis['xrVideoFrames' + $3] = [];
    globalThis['xrVideoFrameDest' + $3] = [];
  }), codecStringCStr, textureWidth, textureHeight, decoderNumber, webCodecsContextIntPtr);
  
  initializationState = WebCodecsState::Initialized;
  return true;
}

bool XRVideoDecodingContext_WebCodecs::StartTextureFrameDecoding(const u8* dataPtr, usize compressedSize, const XRVideoFrameMetadata& metadata, u8* outTexture) {
  if (kVerbose) {
    LOG(INFO) << "StartTextureFrameDecoding() ...";
  }
  
  // This value will be used to wait for decoding to finish
  decodingInProgress = 1;
  
  const int isKeyframe = metadata.isKeyframe;
  // We pass the timestamps as double despite them being integers,
  // since that seems like it might be the best suitable type to pass potentially large integers to JavaScript.
  const double timestampMicroseconds = (metadata.startTimestamp + 500) / 1000;
  const double durationMicroseconds = (metadata.endTimestamp - metadata.startTimestamp + 500) / 1000;
  
  const int dataIntPtr = reinterpret_cast<int>(dataPtr);
  const int outTextureIntPtr = reinterpret_cast<int>(outTexture);
  
  const int decoderNumber = this->decoderNumber;
  const int webCodecsContextIntPtr = reinterpret_cast<int>(this);
  
    // TODO: According to https://www.w3.org/TR/webcodecs-av1-codec-registration/#encodedvideochunk-data ,
  //       the data is expected to be data compliant to the "low-overhead bitstream format".
  //       Is this always guaranteed for the videos that we encode?
  MAIN_THREAD_EM_ASM(({
    const decoder = globalThis['xrVideoDecoder' + $6];
    
    const chunk = new EncodedVideoChunk({
      type: ($2 == 0) ? 'delta' : 'key',
      data: new Uint8Array(HEAPU8.buffer, HEAPU8.byteOffset + $0, $1),
      timestamp: $3,
      duration: $4,
    });
    
    try {
      decoder.decode(chunk);
      // It is safe to set xrVideoFrameDest after calling decoder.decode() because the decode callback can only
      // be called once the current function returns. Also, we only want to set it once decoder.decode() was called without throwing.
      globalThis['xrVideoFrameDest' + $6].push($5);
    } catch (error) {
      console.error(error);
      _WebCodecsWorker_NotifyDecodingThread(/*success*/ 0, $7);
    }
  }), dataIntPtr, compressedSize, isKeyframe, timestampMicroseconds, durationMicroseconds, outTextureIntPtr, decoderNumber, webCodecsContextIntPtr);
  
  return true;
}

bool XRVideoDecodingContext_WebCodecs::WaitForTextureFrameToDecode() {
  if (kVerbose) {
    LOG(INFO) << "WaitForTextureFrameToDecode() ...";
  }
  
  // Note: I tried to call decoder.flush() here because I thought it might help to force the decoder to output all frames passed in so far.
  //       However, even though this does not seem to be mentioned on
  //       https://developer.mozilla.org/en-US/docs/Web/API/VideoDecoder/flush
  //       at the time of writing, Chrome after calling flush() unexpectedly complains:
  //       "A key frame is required after configure() or flush()".
  //       So we don't call it at the moment, hoping that the frames will eventually arrive anyway.
  
  // Wait for decodingInProgress to be changed from one to zero
  emscripten_atomic_wait_u32(/*address*/ &decodingInProgress, /*expectedValue*/ 1, ATOMICS_WAIT_DURATION_INFINITE);
  
  if (kVerbose) {
    LOG(INFO) << "WaitForTextureFrameToDecodeWithWebCodecs(): Done. Result: " << decodedSuccessfully;
  }
  
  return decodedSuccessfully;
}

void XRVideoDecodingContext_WebCodecs::NotifyDecodingThread(bool success) {
  decodedSuccessfully = success;
  decodingInProgress = 0;
  emscripten_atomic_notify(&decodingInProgress, /*count*/ EMSCRIPTEN_NOTIFY_ALL_WAITERS);
}

}

#endif
