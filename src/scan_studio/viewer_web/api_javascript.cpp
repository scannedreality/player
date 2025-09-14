#include <array>

#include <emscripten.h>
#include <emscripten/threading.h>

#include <SDL.h>

#include <loguru.hpp>

#include "scan_studio/viewer_common/render_state.hpp"

#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_common_resources.hpp"

using namespace scan_studio;

extern "C" {

// Dummy definition, just for better code understanding in IDEs:
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

int ScannedReality_initialize_OpenGL(int canvasPtr) {
  const char* canvas = reinterpret_cast<const char*>(canvasPtr);
  
  // Tell emscripten about the existing WebGL context of the HTML5 canvas object that we are going to render to.
  // Note that this use-case does not seem to have been considered in the design of emscripten, unfortunately.
  // It basically seems to assume that it runs on its own with no other existing JavaScript code,
  // and assumes that it is always going to be the emscripten code that will allocate the context.
  //
  // Luckily for us, calling the JavaScript context creation function "getContext()" on a canvas multiple
  // times with the same contextType argument will actually return the same context instance, as documented here:
  //   https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/getContext
  // So, we are going to call emscripten's create_context function below, but it should actually reuse the existing
  // context if one has been allocated on the JavaScript side before.
  EmscriptenWebGLContextAttributes contextAttrs;
  emscripten_webgl_init_context_attributes(&contextAttrs);
  contextAttrs.majorVersion = 2;
  contextAttrs.minorVersion = 0;
  const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context(canvas, &contextAttrs);
  if (context < 0) {
    const EMSCRIPTEN_RESULT errorResult = static_cast<EMSCRIPTEN_RESULT>(context);
    LOG(ERROR) << "emscripten_webgl_create_context() failed with error: " << errorResult << ". One possible reason might be because a context with a different WebGL version might have been created before for this canvas.";
    return 1;
  }
  
  const EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(context);
  if (result != EMSCRIPTEN_RESULT_SUCCESS) {
    LOG(ERROR) << "emscripten_webgl_make_context_current() in main thread failed with error: " << result;
    return 1;
  }
  
  // Load OpenGL function pointers
  if (!gl.Initialize(/*glesMajorVersion*/ 3)) {
    LOG(ERROR) << "Failed to load OpenGL function pointers: " << SDL_GetError();
    return 0;
  }
  
  // Load OpenGL extension function pointers
  if (!glExt.Initialize()) {
    LOG(ERROR) << "Failed to load OpenGL extension function pointers: " << SDL_GetError();
    return 0;
  }
  
  return 1;
}

EMSCRIPTEN_KEEPALIVE
int ScannedReality_initialize(const char* canvas) {
  // Initialize loguru
  loguru::g_preamble_header = false;
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  loguru::g_internal_verbosity = loguru::Verbosity_3;
  
  loguru::Options options;
  options.verbosity_flag = nullptr;  // don't have loguru parse the program options for the verbosity flag
  options.main_thread_name = nullptr;  // don't have loguru set the thread name
  int argc = 1;
  const char* dummyProgramName = "W";
  const char* argv[2] = {dummyProgramName, nullptr};
  loguru::init(argc, const_cast<char**>(argv), options);
  
  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    LOG(ERROR) << "Error calling SDL_Init(): " << SDL_GetError();
    return 0;
  }
  
  if (SDL_GL_LoadLibrary(nullptr) != 0) {
    LOG(ERROR) << "Error calling SDL_GL_LoadLibrary(): " << SDL_GetError();
    return 0;
  }
  
  // Initialize OpenGL-related things
  return emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_II, &ScannedReality_initialize_OpenGL, reinterpret_cast<int>(canvas));
}

EMSCRIPTEN_KEEPALIVE
OpenGLXRVideoCommonResources* XRVideoCommonResources_constructor() {
  OpenGLXRVideoCommonResources* commonResources = new OpenGLXRVideoCommonResources();
  if (!commonResources->Initialize(/*outputLinearColors*/ false, /*use_GL_OVR_multiview2*/ false)) {
    delete commonResources;
    return nullptr;
  }
  return commonResources;
}

EMSCRIPTEN_KEEPALIVE
void XRVideoCommonResources_destroy(OpenGLXRVideoCommonResources* commonResources) {
  delete commonResources;
}

EMSCRIPTEN_KEEPALIVE
OpenGLXRVideo* XRVideo_constructor(int cachedDecodedFrameCount, bool verboseDecoding, OpenGLXRVideoCommonResources* commonResources) {
  // WebGL does not support resource sharing between different contexts, so we use only
  // a single context with emscripten (and proxy all OpenGL calls to the main thread).
  array<unique_ptr<GLContext>, 2> workerThreadContexts;
  
  OpenGLXRVideo* video = new OpenGLXRVideo(std::move(workerThreadContexts));
  if (!video->Initialize(cachedDecodedFrameCount, verboseDecoding, commonResources)) {
    return nullptr;
  }
  return video;
}

EMSCRIPTEN_KEEPALIVE
void XRVideo_destroy(OpenGLXRVideo* video) {
  delete video;
}

EMSCRIPTEN_KEEPALIVE
bool XRVideo_load(OpenGLXRVideo* video, const u8* videoBuffer, int videoBufferSize, int cacheAllFrames, int playbackMode) {
  MemoryInputStream* videoStream = new MemoryInputStream();
  videoStream->SetSource(videoBuffer, videoBufferSize);
  
  // TODO: Support StreamingInputStream
  if (!video->TakeAndOpen(videoStream, /*isStreamingInputStream*/ false, cacheAllFrames)) {
    return false;
  }
  
  video->GetPlaybackState().SetPlaybackMode(static_cast<PlaybackMode>(playbackMode));
  return true;
}

EMSCRIPTEN_KEEPALIVE
int XRVideo_getAsyncLoadState(OpenGLXRVideo* video) {
  return static_cast<int>(video->GetAsyncLoadState());
}

EMSCRIPTEN_KEEPALIVE
bool XRVideo_switchedToMostRecentVideo(OpenGLXRVideo* video) {
  return video->SwitchedToMostRecentVideo();
}

EMSCRIPTEN_KEEPALIVE
void XRVideo_update(OpenGLXRVideo* video, double elapsedSeconds) {
  video->Update(SecondsToNanoseconds(elapsedSeconds));
}

EMSCRIPTEN_KEEPALIVE
XRVideoRenderLock* XRVideo_prepareRenderLock(OpenGLXRVideo* video) {
  XRVideoRenderLock* renderLock = video->CreateRenderLock().release();
  
  if (renderLock) {
    OpenGLRenderState renderState;
    renderLock->PrepareFrame(&renderState);
  }
  
  return renderLock;
}

EMSCRIPTEN_KEEPALIVE
void XRVideo_render(const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData, int useSurfaceNormalShading, XRVideoRenderLock* renderLock) {
  if (!renderLock) { return; }
  
  OpenGLRenderState renderState;
  renderLock->PrepareView(/*viewIndex*/ 0, /*multiViewIndex*/ 0, useSurfaceNormalShading, &renderState);
  renderLock->SetModelViewProjection(/*viewIndex*/ 0, /*multiViewIndex*/ 0, columnMajorModelViewData, columnMajorModelViewProjectionData);
  renderLock->RenderView(&renderState);
}

EMSCRIPTEN_KEEPALIVE
void XRVideo_destroyRenderLock(XRVideoRenderLock* renderLock) {
  delete renderLock;
}

EMSCRIPTEN_KEEPALIVE
bool XRVideo_setPlaybackMode(OpenGLXRVideo* video, int playbackMode) {
  video->GetPlaybackState().SetPlaybackMode(static_cast<PlaybackMode>(playbackMode));
  return true;
}

EMSCRIPTEN_KEEPALIVE
double XRVideo_getStartTimestamp(OpenGLXRVideo* video) {
  return NanosecondsToSeconds(video->Index().GetVideoStartTimestamp());
}

EMSCRIPTEN_KEEPALIVE
double XRVideo_getEndTimestamp(OpenGLXRVideo* video) {
  return NanosecondsToSeconds(video->Index().GetVideoEndTimestamp());
}

EMSCRIPTEN_KEEPALIVE
double XRVideo_getPlaybackTimestamp(OpenGLXRVideo* video) {
  PlaybackState& playbackState = video->GetPlaybackState();
  playbackState.Lock();
  const double playbackTime = NanosecondsToSeconds(playbackState.GetPlaybackTime());
  playbackState.Unlock();
  return playbackTime;
}

EMSCRIPTEN_KEEPALIVE
void XRVideo_seek(OpenGLXRVideo* video, double timestamp, int forward) {
  video->Seek(SecondsToNanoseconds(timestamp), forward);
}

EMSCRIPTEN_KEEPALIVE
bool XRVideo_isBuffering(OpenGLXRVideo* video) {
  return video->IsBuffering();
}

EMSCRIPTEN_KEEPALIVE
double XRVideo_getBufferingProgressPercent(OpenGLXRVideo* video) {
  return video->GetBufferingProgressPercent();
}

}
