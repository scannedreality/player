#pragma once

#ifdef __EMSCRIPTEN__

#include <string>
#include <vector>

#include <emscripten.h>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

class XRVideoDecodingContext;
struct XRVideoFrameMetadata;

// TODO: The use of WebCodecs has been disabled because:
//       1. On my Android phone, it returned frames in RGBA instead of YUV format.
//          The best way to deal with this would be to use the returned frame objects in calls to webGl.texImage2D()
//          to convert them into textures, ensuring that we always get a well-defined texture format (RGBA),
//          and also avoiding a potential GPU -> CPU -> GPU round-trip transfer in case the decoder is on the GPU.
//          However, then we would also need to pass these textures on to the OpenGLXRVideoFrame objects via
//          a special-case path, and implement a special-case shader to render them, since we usually render YUV textures.
//          The textures will also consume a lot more memory as RGBA instead of YUV.
//          Perhaps a future version of the WebCodecs API might also allow us to set a desired target format?
//       2. Comparing Firefox without WebCodecs and Chrome with WebCodecs, with WebCodecs decoding seemed to be a little bit,
//          but not significantly faster than our WASM version of dav1d.
//          It is thinkable that we might get better speed by optimizing our use of dav1d a bit
//          (submitting more than one frame at a time), and perhaps implementing some parts of dav1d in WASM SIMD
//          (though that won't work in Safari as of now). Perhaps a future version of the WebCodecs API might also
//          allow us to configure the number of threads used for software decoding (whose supposedly too low count
//          I guess is the reason for the WebCodecs version to be only marginally faster)?

// #ifdef __EMSCRIPTEN__
//   // On the first call, check for WebCodecs support in the browser. If supported, use this for texture decoding instead of dav1d.
//   auto& webCodecsContext = decodingContext->WebCodecsContext();
//   
//   if (webCodecsContext.GetInitializationState() == XRVideoDecodingContext_WebCodecs::WebCodecsState::Uninitialized) {
//     webCodecsContext.TryInitializeDecoder(dataPtr, compressedSize, metadata.textureWidth, metadata.textureHeight);
//   }
//   
//   if (webCodecsContext.GetInitializationState() == XRVideoDecodingContext_WebCodecs::WebCodecsState::Initialized) {
//     return webCodecsContext.StartTextureFrameDecoding(dataPtr, compressedSize, metadata, outTexture);
//   }
// #else
  // (void) metadata;
  // (void) outTextureLuma;
  // (void) outTextureChromaU;
  // (void) outTextureChromaV;
// #endif

// #ifdef __EMSCRIPTEN__
//   auto& webCodecsContext = decodingContext->WebCodecsContext();
//   if (webCodecsContext.GetInitializationState() == XRVideoDecodingContext_WebCodecs::WebCodecsState::Initialized) {
//     return webCodecsContext.WaitForTextureFrameToDecode();
//   }
// #endif

class XRVideoDecodingContext_WebCodecs {
 public:
  enum class WebCodecsState {
    /// No attempt to initialize WebCodecs has been made yet.
    Uninitialized,
    
    /// A WebCodecs decoder has been initialized successfully.
    Initialized,
    
    /// Either WebCodecs are not supported in the browser at all,
    /// or the specific decoding config that is required for the XRVideo file is not supported.
    Failed
  };
  
  void CreateWebWorker();
  
  bool WasWebWorkerStartRequested();
  
  bool HasWebWorkerStarted();
  
  bool TryInitializeDecoder(const u8* chunkWithHeaderPtr, usize compressedSize, u32 textureWidth, u32 textureHeight);
  
  bool StartTextureFrameDecoding(const u8* dataPtr, usize compressedSize, const XRVideoFrameMetadata& metadata, u8* outTexture);
  
  bool WaitForTextureFrameToDecode();
  
  inline WebCodecsState GetInitializationState() const { return initializationState; }
  
  /// Internal function, do not call from outside.
  /// TODO: Should not be public
  void NotifyDecodingThread(bool success);
  
 private:
  /// The current initialization state. See WebCodecsState.
  WebCodecsState initializationState = WebCodecsState::Uninitialized;
  
  /// A number that is supposed to be unique for the current browser page, allowing us to store
  /// global JavaScript variables without collisions.
  int decoderNumber;
  
  /// The codec string required by WebCodecs.
  string codecString;
  
  /// Value used to wait for decoding to finish using emscripten_atomic_wait_u32() and emscripten_atomic_notify()
  u32 decodingInProgress;
  
  /// Whether the last queued frame was decoded successfully
  bool decodedSuccessfully;
};

}

#endif
