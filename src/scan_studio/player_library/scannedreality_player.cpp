#include "scan_studio/player_library/scannedreality_player.h"

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/external/external_xrvideo.hpp"

using namespace scan_studio;


// --- Logging ---

struct SRPlayer_LogCallbackHandle {
  string loguruID;
  SRPlayer_LogCallback callback;
  void* userData;
};

static void LoguruLogCallback(void* userData, const loguru::Message& msg) {
  SRPlayer_LogCallbackHandle* callbackData = static_cast<SRPlayer_LogCallbackHandle*>(userData);
  
  ostringstream msgText;
  msgText << msg.preamble << msg.prefix << msg.message;
  callbackData->callback(static_cast<int32_t>(msg.verbosity), msgText.str().c_str(), msg.filename, msg.line, callbackData->userData);
}

void SRPlayer_InitializeLogging(SRBool32 logToStdErr) {
  // Initialize loguru.
  // Notice that we disable logging by default, setting g_stderr_verbosity to OFF.
  // This way, we will only log something if a user-provided logCallback is given here.
  loguru::g_preamble_date = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = logToStdErr ? 2 : loguru::Verbosity_OFF;
  
  // loguru expects argc >= 1, so we create dummy arguments here.
  int argc = 1;
  const char* argv[2] = {"ScannedRealityPlayer", nullptr};
  
  loguru::Options options;
  options.verbosity_flag = nullptr;                        // don't have loguru parse the program options for the verbosity flag
  options.main_thread_name = nullptr;                      // don't have loguru set the thread name
  options.signal_options = loguru::SignalOptions::none();  // don't have loguru mess with signal handlers in external applications that use this library
  loguru::init(argc, const_cast<char**>(argv), options);
}

SRPlayer_LogCallbackHandle* SRPlayer_AddLogCallback(SRPlayer_LogCallback logCallback, void* logCallbackUserData) {
  if (!logCallback) { return nullptr; }
  
  SRPlayer_LogCallbackHandle* callbackData = new SRPlayer_LogCallbackHandle();
  
  callbackData->callback = logCallback;
  callbackData->userData = logCallbackUserData;
  
  // Generate a unique ID for the callback for loguru from the allocated pointer
  ostringstream loguruID;
  loguruID << static_cast<void*>(callbackData);
  callbackData->loguruID = loguruID.str();
  
  loguru::add_callback(callbackData->loguruID.c_str(), &LoguruLogCallback, /*user_data*/ callbackData, /*forward all verbosity levels to the callback*/ loguru::Verbosity_MAX);
  
  return callbackData;
}

SRBool32 SRPlayer_RemoveLogCallback(SRPlayer_LogCallbackHandle* callbackHandle) {
  if (!callbackHandle) { return SRV_FALSE; }
  
  SRPlayer_LogCallbackHandle* callbackData = static_cast<SRPlayer_LogCallbackHandle*>(callbackHandle);
  
  return loguru::remove_callback(callbackData->loguruID.c_str());
}


// --- XRVideo ---

SRPlayer_XRVideo* SRPlayer_XRVideo_NewExternal(uint32_t cachedDecodedFrameCount, SRPlayer_XRVideo_External_Config* config) {
  ExternalXRVideo* video = new ExternalXRVideo(*config);
  if (!video->Initialize(cachedDecodedFrameCount, /*verboseDecoding*/ false, /*commonResources*/ nullptr)) {
    delete video;
    return nullptr;
  }
  // Note: The single reinterpret_cast below should work fine as long as we avoid multiple inheritance (one class inheriting two or more base classes).
  //       Otherwise, consider dynamic_cast:
  // return reinterpret_cast<SRPlayer_XRVideo*>(dynamic_cast<XRVideo*>(video));
  return reinterpret_cast<SRPlayer_XRVideo*>(video);
}

void SRPlayer_XRVideo_Destroy(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  delete videoImpl;
}

SRBool32 SRPlayer_XRVideo_LoadFile(SRPlayer_XRVideo* video, const char* path, SRBool32 cacheAllFrames, uint32_t playbackMode) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  
  IfstreamInputStream* inputStream = new IfstreamInputStream();
  if (!inputStream->Open(path)) {
    LOG(ERROR) << "Failed to open file: " << path;
    delete inputStream;
    return false;
  }
  
  if (!videoImpl->TakeAndOpen(inputStream, /*isStreamingInputStream*/ false, cacheAllFrames)) {
    return false;
  }
  
  videoImpl->GetPlaybackState().SetPlaybackMode(static_cast<PlaybackMode>(playbackMode));
  
  return true;
}

/// Implementation of InputStream to support C-API-Callback based input.
/// Acts as a simple wrapper that directly passes each function call to an underlying callback.
class CallbackInputStream : public InputStream {
 public:
  inline CallbackInputStream(SRPlayer_InputCallbacks callbacks)
      : callbacks(callbacks) {}
  
  CallbackInputStream(const CallbackInputStream& other) = delete;
  CallbackInputStream& operator= (const CallbackInputStream& other) = delete;
  
  CallbackInputStream(CallbackInputStream&& other) = default;
  CallbackInputStream& operator= (CallbackInputStream&& other) = default;
  
  ~CallbackInputStream() { Close(); }
  
  inline void Close() { callbacks.closeCallback(callbacks.userData); }
  
  virtual inline usize Read(void* data, usize size) override { return callbacks.readCallback(data, size, callbacks.userData); }
  virtual inline bool Seek(u64 offsetFromStart) override { return callbacks.seekCallback(offsetFromStart, callbacks.userData); }
  virtual inline u64 SizeInBytes() override { return callbacks.sizeInBytesCallback(callbacks.userData); }
  
 private:
  SRPlayer_InputCallbacks callbacks;
};

SRBool32 SRPlayer_XRVideo_LoadCustom(SRPlayer_XRVideo* video, SRPlayer_InputCallbacks* input, SRBool32 cacheAllFrames, uint32_t playbackMode) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  
  CallbackInputStream* inputStream = new CallbackInputStream(*input);
  
  if (!videoImpl->TakeAndOpen(inputStream, /*isStreamingInputStream*/ false, cacheAllFrames)) {
    return false;
  }
  
  videoImpl->GetPlaybackState().SetPlaybackMode(static_cast<PlaybackMode>(playbackMode));
  
  return true;
}

SRPlayer_AsyncLoadState SRPlayer_XRVideo_GetAsyncLoadState(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return static_cast<SRPlayer_AsyncLoadState>(videoImpl->GetAsyncLoadState());
}

SRBool32 SRPlayer_XRVideo_SwitchedToMostRecentVideo(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->SwitchedToMostRecentVideo();
}

void SRPlayer_XRVideo_SetPlaybackMode(SRPlayer_XRVideo* video, uint32_t playbackMode) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  videoImpl->GetPlaybackState().SetPlaybackMode(static_cast<PlaybackMode>(playbackMode));
}

int64_t SRPlayer_XRVideo_GetStartTimestampNanoseconds(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->Index().GetVideoStartTimestamp();
}

int64_t SRPlayer_XRVideo_GetEndTimestampNanoseconds(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->Index().GetVideoEndTimestamp();
}

int64_t SRPlayer_XRVideo_GetPlaybackTimestampNanoseconds(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  PlaybackState& playbackState = videoImpl->GetPlaybackState();
  playbackState.Lock();
  const int64_t playbackTime = playbackState.GetPlaybackTime();
  playbackState.Unlock();
  return playbackTime;
}

void SRPlayer_XRVideo_Seek(SRPlayer_XRVideo* video, int64_t timestampNanoseconds, SRBool32 forward) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  videoImpl->Seek(timestampNanoseconds, forward);
}

SRBool32 SRPlayer_XRVideo_IsBuffering(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->IsBuffering();
}

float SRPlayer_XRVideo_GetBufferingProgressPercent(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->GetBufferingProgressPercent();
}

int64_t SRPlayer_XRVideo_Update(SRPlayer_XRVideo* video, int64_t elapsedNanoseconds) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  return videoImpl->Update(elapsedNanoseconds);
}

SRPlayer_XRVideoRenderLock* SRPlayer_XRVideo_PrepareFrame(SRPlayer_XRVideo* video) {
  XRVideo* videoImpl = reinterpret_cast<XRVideo*>(video);
  
  XRVideoRenderLock* renderLock = videoImpl->CreateRenderLock().release();
  return reinterpret_cast<SRPlayer_XRVideoRenderLock*>(renderLock);
}

void SRPlayer_XRVideoRenderLock_Release(SRPlayer_XRVideoRenderLock* renderLock) {
  XRVideoRenderLock* renderLockImpl = reinterpret_cast<XRVideoRenderLock*>(renderLock);
  delete renderLockImpl;
}

SRBool32 SRPlayer_XRVideoRenderLock_External_GetData(SRPlayer_XRVideoRenderLock* renderLock, SRPlayer_XRVideoRenderLock_External_Data* data) {
  XRVideoRenderLock* renderLockImpl = reinterpret_cast<XRVideoRenderLock*>(renderLock);
  // Note: The single reinterpret_cast below should work fine as long as we avoid multiple inheritance (one class inheriting two or more base classes).
  //       Otherwise, consider dynamic_cast:
  // ExternalXRVideoRenderLock* externalRenderLock = dynamic_cast<ExternalXRVideoRenderLock*>(renderLockImpl);
  ExternalXRVideoRenderLock* externalRenderLock = reinterpret_cast<ExternalXRVideoRenderLock*>(renderLockImpl);
  if (externalRenderLock == nullptr) { return false; }
  
  data->keyframeUserData = externalRenderLock->GetKeyframe().GetFrame()->UserData();
  const auto* previousFrameReadLock = externalRenderLock->GetPreviousFrame();
  data->previousFrameUserData = previousFrameReadLock ? previousFrameReadLock->GetFrame()->UserData() : nullptr;
  data->currentFrameUserData = externalRenderLock->GetDisplayFrame().GetFrame()->UserData();
  data->currentIntraFrameTime = renderLockImpl->CurrentIntraFrameTime();
  
  return true;
}
