#include "scan_studio/viewer_common/common.hpp"

#ifndef __ANDROID__
  #include <SDL.h>
#endif

#include "scan_studio/viewer_common/audio/audio_sdl.hpp"

#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_common_resources.hpp"

#ifdef HAVE_OPENGL
  #include "scan_studio/viewer_common/opengl/context_egl.hpp"
  #include "scan_studio/viewer_common/opengl/context_sdl.hpp"
  #include "scan_studio/viewer_common/opengl/extensions.hpp"
  #include "scan_studio/viewer_common/opengl/loader.hpp"
  #include "scan_studio/viewer_common/opengl/util.hpp"
  
  #include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo.hpp"
  #include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo_common_resources.hpp"
#endif
#ifdef HAVE_VULKAN
  #include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo.hpp"
  #include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo_common_resources.hpp"
#endif
#ifdef __APPLE__
  #include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo.hpp"
  #include "scan_studio/viewer_common/xrvideo/metal/metal_xrvideo_common_resources.hpp"
#endif

namespace scan_studio {

ViewerCommon::ViewerCommon(RendererType rendererType, bool cacheAllFrames)
    : rendererType(rendererType),
      cacheAllFrames(cacheAllFrames) {}

ViewerCommon::~ViewerCommon() {
  Deinitialize();
}

#ifdef HAVE_VULKAN
void ViewerCommon::InitializeVulkan(int maxFramesInFlightCount, VkSampleCountFlagBits msaaSamples, VkRenderPass renderPass, VulkanDevice* device) {
  this->maxFramesInFlightCount = maxFramesInFlightCount;
  vulkan.msaaSamples = msaaSamples;
  vulkan.renderPass = renderPass;
  vulkan.device = device;
}
#endif

#ifdef __APPLE__
void ViewerCommon::InitializeMetal(int maxFramesInFlightCount, MetalLibraryCache* libraryCache, MetalRenderPassDescriptor* renderPassDesc, MTL::Device* device) {
  this->maxFramesInFlightCount = maxFramesInFlightCount;
  metal.libraryCache = libraryCache;
  metal.renderPassDesc = renderPassDesc;
  metal.device = device;
}
#endif

void ViewerCommon::Initialize() {
  #ifdef HAVE_OPENGL
    if (IsOpenGLRendererType(rendererType)) {
      int glesMajorVersion = -1;
      if (rendererType == RendererType::OpenGL_ES_3_0 || rendererType == RendererType::OpenGL_4_1) {
        glesMajorVersion = 3;
      } else {
        LOG(ERROR) << "Renderer type not supported: " << static_cast<int>(rendererType);
        // TODO: Quit and let the user know about the problem
      }
      
      // Load OpenGL function pointers
      if (!gl.Initialize(glesMajorVersion)) {
        #ifdef __ANDROID__
          LOG(ERROR) << "Failed to load OpenGL function pointers";
        #else
          LOG(ERROR) << "Failed to load OpenGL function pointers: " << SDL_GetError();
        #endif
        // TODO: Quit and let the user know about the problem
      }
      
      // Load OpenGL extension function pointers
      if (!glExt.Initialize()) {
        #ifdef __ANDROID__
          LOG(ERROR) << "Failed to load OpenGL extension function pointers";
        #else
          LOG(ERROR) << "Failed to load OpenGL extension function pointers: " << SDL_GetError();
        #endif
        // TODO: Quit and let the user know about the problem
      }
    
      #ifndef __ANDROID__
        // Try to enable "adaptive vsync". If that fails, enable standard vsync. See: https://wiki.libsdl.org/SDL_GL_SetSwapInterval
        if (SDL_GL_SetSwapInterval(-1) == 0) {
          LOG(1) << "Adaptive VSync enabled";
        } else if (SDL_GL_SetSwapInterval(1) == 0) {
          LOG(1) << "VSync enabled";
        } else {
          LOG(WARNING) << "Failed to enable VSync";
        }
      #endif
      
      // Query support for GL_EXT_sRGB
      // TODO: It would be nice to use this, but SDL does not support letting WGL create an sRGB render target.
      // usingExtension_GL_EXT_sRGB = SDL_GL_ExtensionSupported("GL_EXT_sRGB");
      // LOG(INFO) << "GL_EXT_sRGB extension used: " << (usingExtension_GL_EXT_sRGB ? "yes" : "no");
      
      // An sRGB framebuffer was on by default on a Radeon RX 6600, and off everywhere else I tested (non-AMD GPUs), so force it off.
      // GL_FRAMEBUFFER_SRGB is non-ES OpenGL only though :( Not sure whether we can use a proper OpenGL ES way.
      // SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0); does not help anything.
      // Might just be a bug in AMD's OpenGL ES implementation :(
      #if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        gl.glDisable(/*GL_FRAMEBUFFER_SRGB*/ 0x8DB9);
      #endif
    }
  #endif
}

void ViewerCommon::Deinitialize() {
  audio.reset();
  
  // Note: Deinitialize() is not run on the main thread with emscripten.
  //       This means that if we add OpenGL calls here, they must be proxied to the main thread (see e.g., OpenGLXRVideo::Destroy() for how to do that).
  xrVideoRenderLock.reset();
  xrVideo.reset();
  xrVideoCommonResources.reset();
}

bool ViewerCommon::InitializeGraphicObjects(int viewCount, bool verboseDecoding, void* openGLContextCreationUserPtr) {
  (void) viewCount;
  
  if (xrVideo) {
    // TODO: We should check here whether we must recreate some resources
    return true;
  }
  
  if (rendererType == RendererType::Vulkan_1_0 || rendererType == RendererType::Vulkan_1_0_OpenXR_1_0) {
    #ifdef HAVE_VULKAN
    VulkanXRVideoCommonResources* commonResources = new VulkanXRVideoCommonResources();
    if (!commonResources->Initialize(maxFramesInFlightCount, vulkan.msaaSamples, vulkan.renderPass, vulkan.device)) {
      LOG(ERROR) << "Failed to initialize VulkanXRVideoCommonResources";
      delete commonResources;
      return false;
    }
    xrVideoCommonResources.reset(commonResources);
    xrVideo.reset(new VulkanXRVideo(viewCount, maxFramesInFlightCount, vulkan.device));
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    MetalXRVideoCommonResources* commonResources = new MetalXRVideoCommonResources();
    // Note: So far, we never use sRGB render targets with the Metal render path, so we always set outputLinearColors to false, which will cause the output of sRGB colors.
    if (!commonResources->Initialize(/*outputLinearColors*/ false, /*usesInverseZ*/ false, metal.renderPassDesc, metal.libraryCache, metal.device)) {
      LOG(ERROR) << "Failed to initialize MetalXRVideoCommonResources";
      delete commonResources;
      return false;
    }
    xrVideoCommonResources.reset(commonResources);
    xrVideo.reset(new MetalXRVideo(viewCount, maxFramesInFlightCount, metal.device));
    #endif
  } else {  // if (rendererType == RendererType::OpenGL) {
    #ifdef HAVE_OPENGL
      // Create OpenGL contexts for the decoding and transfer threads that share names with the current context
      array<unique_ptr<GLContext>, 2> workerThreadContexts;
      
      #if defined(__ANDROID__)
        EGLConfig* eglConfig = reinterpret_cast<EGLConfig*>(openGLContextCreationUserPtr);
        
        for (int i = 0; i < workerThreadContexts.size(); ++ i) {
          GLContextEGL* newContext = new GLContextEGL();
          if (!newContext->Create(*eglConfig, /*shareResources*/ true)) {
            LOG(ERROR) << "Failed to create an OpenGL context for a worker thread";
            delete newContext;
            return false;
          }
          workerThreadContexts[i].reset(newContext);
        }
      #elif defined(__EMSCRIPTEN__)
        // WebGL does not support resource sharing between different contexts, so we use only
        // a single context with emscripten (and proxy all OpenGL calls to the main thread).
        (void) openGLContextCreationUserPtr;
      #else
        SDL_Window* sdlWindow = reinterpret_cast<SDL_Window*>(openGLContextCreationUserPtr);
        
        const SDL_GLContext oldContext = SDL_GL_GetCurrentContext();
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        
        for (int i = 0; i < workerThreadContexts.size(); ++ i) {
          SDL_GLContext newContext = SDL_GL_CreateContext(sdlWindow);
          if (newContext == nullptr) {
            LOG(ERROR) << "Failed to create an OpenGL context for a worker thread, error: " << SDL_GetError();
            return false;
          }
          
          // SDL_GL_CreateContext() also makes the new context current, so we have to revert to the old context here
          if (SDL_GL_MakeCurrent(sdlWindow, oldContext) != 0) {
            LOG(ERROR) << "Failed to make oldContext current, error: " << SDL_GetError();
            SDL_GL_DeleteContext(newContext);
            return false;
          }
          
          workerThreadContexts[i] = make_unique<GLContextSDL>(sdlWindow, newContext);
        }
      #endif
      
      OpenGLXRVideoCommonResources* commonResources = new OpenGLXRVideoCommonResources();
      // Note: So far, we never use sRGB render targets with the OpenGL render path, so we always set outputLinearColors to false, which will cause the output of sRGB colors.
      if (!commonResources->Initialize(/*outputLinearColors*/ false, /*use_GL_OVR_multiview2*/ false)) {
        LOG(ERROR) << "Failed to initialize OpenGLXRVideoCommonResources";
        delete commonResources;
        return false;
      }
      xrVideoCommonResources.reset(commonResources);
      xrVideo.reset(new OpenGLXRVideo(std::move(workerThreadContexts)));
    #else
      (void) openGLContextCreationUserPtr;
    #endif
  }
  
  // Initialize the XRVideo, specifying the number of decoded frames to cache.
  //
  // Note that a certain minimum number of frames must be used here, since for example,
  // a single dependent (non-key)frame may require its direct predecessor as well as its
  // base keyframe for rendering, so this already makes three frames in the cache required
  // to render a single one. Also, an XRVideo holds on to its locked frames for being able
  // to render them while seeking, so there must be space for the seeked-to frames in addition
  // to the old frames that are being held on.
  constexpr int defaultDecodedFrameCountToCache = 60;
  const int cachedDecodedFrameCount = cacheAllFrames ? 0 : defaultDecodedFrameCountToCache;
  if (!xrVideo->Initialize(cachedDecodedFrameCount, verboseDecoding, xrVideoCommonResources.get())) {
    // TODO: Report error to user properly
    LOG(ERROR) << "xrVideo->Initialize() failed";
    return false;
  }
  
  return true;
}

bool ViewerCommon::OpenFile(bool preReadCompleteFile, const filesystem::path& videoPath) {
  // Open the video file and if enabled, pre-read it.
  // Pre-reading is used for the web viewer, where at the time of writing this (March 2023),
  // file operations in WASM were a performance problem (but a new "WASMFS" file I/O backend
  // for emscripten was in the works that aims at better performance).
  constexpr bool isStreamingInputStream = false;
  unique_ptr<InputStream> inputStream = OpenAssetUnique(videoPath.c_str(), /*isRelativeToAppPath*/ false);
  if (inputStream == nullptr) {
    LOG(ERROR) << "Failed to open video file at " << videoPath << "!";
    // TODO: Report error to user properly
  }
  if (preReadCompleteFile) {
    vector<u8> fileData;
    if (!inputStream->ReadAll(&fileData)) {
      LOG(ERROR) << "Failed to read video file at " << videoPath << "!";
      // TODO: Report error to user properly
    }
    
    unique_ptr<InputStream> vectorInputStream(new VectorInputStream(std::move(fileData)));
    inputStream.swap(vectorInputStream);
  }
  
  // Try opening an audio file that may be associated with the video file
  unique_ptr<InputStream> audioInputStream;
  const string filename = videoPath.filename().string();
  if (filename.size() >= 3) {
    const filesystem::path audioPath = videoPath.parent_path() / (filename.substr(0, filename.size() - 3) + "wav");
    audioInputStream = OpenAssetUnique(audioPath, /*isRelativeToAppPath*/ false);
    
    // TODO: Since we don't have a separate I/O thread for audio yet,
    //       we currently always pre-read the whole audio file to prevent audio hiccups due to slow disk reads.
    if (/*preReadCompleteFile*/ true && audioInputStream) {
      vector<u8> fileData;
      if (!audioInputStream->ReadAll(&fileData)) {
        LOG(ERROR) << "Failed to read audio file at " << audioPath << "!";
        // TODO: Report error to user properly
      }
      
      unique_ptr<InputStream> vectorInputStream(new VectorInputStream(std::move(fileData)));
      audioInputStream.swap(vectorInputStream);
    }
  }
  
  // Load the XRVideo file
  if (!xrVideo->TakeAndOpen(inputStream.release(), isStreamingInputStream, cacheAllFrames)) {
    // TODO: Report error to user properly
    LOG(ERROR) << "xrVideo->TakeAndOpen() failed";
    return false;
  }
  
  xrVideo->GetPlaybackState().SetPlaybackMode(PlaybackMode::Loop);
  
  // Initialize the audio playback
  if (audioInputStream) {
    audio.reset(new SDLAudio());
    if (!audio->Initialize()) {
      // TODO: Report error to user properly
      LOG(ERROR) << "Failed to initialize audio playback";
      audio.reset();
      audioInputStream.reset();
    } else {
      if (!audio->TakeAndOpen(audioInputStream.release())) {
        // TODO: Report error to user properly
        LOG(ERROR) << "Failed to parse the audio file";
        audio.reset();
      } else {
        xrVideo->GetPlaybackState().Lock();
        audio->SetPlaybackMode(xrVideo->GetPlaybackState().GetPlaybackMode());
        xrVideo->GetPlaybackState().Unlock();
        
        audio->Play();
      }
    }
  }
  
  return true;
}

void ViewerCommon::PrepareFrame(s64 predictedDisplayTimeNanoseconds, bool paused, RenderState* renderState) {
  // Advance the XRVideo's playback time if not paused
  const s64 elapsedNanoseconds = lastDisplayTimeInitialized ? (predictedDisplayTimeNanoseconds - lastDisplayTimeNanoseconds) : 0;
  lastDisplayTimeNanoseconds = predictedDisplayTimeNanoseconds;
  lastDisplayTimeInitialized = true;
  
  s64 videoUpdateNanoseconds;
  
  if (audio) {
    videoUpdateNanoseconds = GetAudioSynchronizedPlaybackDelta(paused, elapsedNanoseconds, xrVideo.get(), audio.get());
  } else {
    videoUpdateNanoseconds = paused ? 0 : elapsedNanoseconds;
  }
  
  xrVideo->Update(videoUpdateNanoseconds);
  
  xrVideoRenderLock = xrVideo->CreateRenderLock();
  if (xrVideoRenderLock) {
    xrVideoRenderLock->PrepareFrame(renderState);
  }
}

void ViewerCommon::PrepareView(int viewIndex, bool useSurfaceNormalShading, RenderState* renderState) {
  if (xrVideoRenderLock) {
    xrVideoRenderLock->PrepareView(viewIndex, /*flipBackFaceCulling*/ false, useSurfaceNormalShading, renderState);
  }
}

void ViewerCommon::RenderView(RenderState* renderState) {
  if (xrVideoRenderLock) {
    xrVideoRenderLock->RenderView(renderState);
  }
}

void ViewerCommon::EndFrame() {
  xrVideoRenderLock.reset();
}

bool ViewerCommon::SupportsLateModelViewProjectionSetting() {
  return rendererType == RendererType::Vulkan_1_0_OpenXR_1_0;
}

void ViewerCommon::SetModelViewProjection(int viewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData) {
  if (xrVideoRenderLock) {
    xrVideoRenderLock->SetModelViewProjection(viewIndex, /*multiViewIndex*/ 0, columnMajorModelViewData, columnMajorModelViewProjectionData);
  }
}

s64 ViewerCommon::GetAudioSynchronizedPlaybackDelta(bool paused, s64 elapsedNanoseconds, XRVideo* xrVideo, SDLAudio* audio) {
  constexpr bool kDebugAudio = false;
  
  const bool isCurrentlyBuffering = xrVideo->IsBuffering();
  if (paused || isCurrentlyBuffering) {
    if (audio->IsPlaying()) {
      if (kDebugAudio) { LOG(INFO) << "Audio debug: Pausing audio since video is paused or buffering (paused: " << paused << ", buffering: " << isCurrentlyBuffering << ")"; }
      
      audio->Pause();
    }
    return 0;
  }
  
  // Start audio?
  const bool previouslyPlaying = audio->IsPlaying();
  if (!previouslyPlaying) {
    if (kDebugAudio) { LOG(INFO) << "Audio debug: Video playing, but audio is not. Starting the audio."; }
    
    auto& videoPlaybackState = xrVideo->GetPlaybackState();
    videoPlaybackState.Lock();
    const PlaybackMode playbackMode = videoPlaybackState.GetPlaybackMode();
    const bool playForward = videoPlaybackState.PlayingForward();
    const s64 playbackTime = videoPlaybackState.GetPlaybackTime();
    videoPlaybackState.Unlock();
    
    audio->SetPlaybackMode(playbackMode);
    audio->SetPlaybackPosition(playbackTime - xrVideo->Index().GetVideoStartTimestamp(), playForward);
    
    audio->Play();
  }
  
  // Compute the video time delta to use based on the time that has passed
  s64 videoDeltaTime = elapsedNanoseconds;
  
  // If available, apply an update to the video playback time.
  // TODO: Rather than using chrono::steady_clock::now() (and some guessed offset) as predictedDisplayTime below,
  //       get a more accurate estimate from the display system. For example, for OpenXR,
  //       the XR_KHR_convert_timespec_time and XR_KHR_win32_convert_performance_counter_time
  //       extensions should be used to convert OpenXR's XrTime to a system time.
  //       We cannot directly use the `s64 predictedDisplayTimeNanoseconds` that is currently
  //       passed to ViewerCommon::PrepareFrame() because we don't know the current time in its clock.
  const chrono::steady_clock::time_point predictedDisplayTime = chrono::steady_clock::now() + chrono::duration<s64, nano>(SecondsToNanoseconds(0.033));
  s64 predictedPlaybackTimeNanoseconds;
  if (audio->PredictPlaybackTimeAt(predictedDisplayTime, &predictedPlaybackTimeNanoseconds)) {
    // Since PredictPlaybackTimeAt() may change its estimate abrubtly as new information comes in,
    // we smoothly change videoDeltaTime here to reduce the difference rather than using predictedPlaybackTimeNanoseconds
    // as display time directly.
    auto& videoPlaybackState = xrVideo->GetPlaybackState();
    videoPlaybackState.Lock();
    // const PlaybackMode playbackMode = videoPlaybackState.GetPlaybackMode();
    const bool playForward = videoPlaybackState.PlayingForward();
    const s64 videoPlaybackTime = videoPlaybackState.GetPlaybackTime() - xrVideo->Index().GetVideoStartTimestamp() + (playForward ? 1 : -1) * videoDeltaTime;  // TODO: Not accounting for the playbackMode wrap-around here
    videoPlaybackState.Unlock();
    
    // If one of the playback states has looped and the other has not, discard the sample
    const s64 duration = xrVideo->Index().GetVideoEndTimestamp() - xrVideo->Index().GetVideoStartTimestamp();
    const s64 OneThirdTimestamp = static_cast<s64>((1.f / 3.f) * duration);
    const s64 TwoThirdTimestamp = static_cast<s64>((2.f / 3.f) * duration);
    
    if ((predictedPlaybackTimeNanoseconds < OneThirdTimestamp && videoPlaybackTime > TwoThirdTimestamp) ||
        (predictedPlaybackTimeNanoseconds > TwoThirdTimestamp && videoPlaybackTime < OneThirdTimestamp)) {
      if (kDebugAudio) { LOG(INFO) << "Audio sync debug: Audio playback in last third, video playback in first third"; }
    } else {
      const s64 timeOffset = predictedPlaybackTimeNanoseconds - videoPlaybackTime;
      
      const float updateFactor = std::pow(0.1f, NanosecondsToSeconds(elapsedNanoseconds));
      const s64 smoothTimeOffset = /*updateFactor * 0*/ + (1.0f - updateFactor) * timeOffset;
      
      videoDeltaTime += smoothTimeOffset;
      
      if (kDebugAudio) { LOG(INFO) << "Audio sync debug: Applying smoothTimeOffset of " << NanosecondsToSeconds(smoothTimeOffset); }
    }
  }
  
  return videoDeltaTime;
}

}
