#include "scan_studio/viewer_common/display_flatscreen.hpp"

#include <libvis/io/filesystem.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

#include <libvis/vulkan/transform_matrices.h>

#ifdef HAVE_VULKAN
  #include <libvis/vulkan/framebuffer.h>
  
  #ifndef __ANDROID__
    #include "scan_studio/viewer_common/platform/render_window_sdl_vulkan.hpp"
  #endif
  #include "scan_studio/viewer_common/xrvideo/vulkan/vulkan_xrvideo.hpp"
#endif

#ifdef __APPLE__
  #include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"
#endif

#ifndef __ANDROID__
  #include "scan_studio/viewer_common/platform/render_window_sdl_opengl.hpp"
#endif

#include "scan_studio/common/xrvideo_file.hpp"
#include "scan_studio/common/sRGB.hpp"

#ifdef HAVE_OPENGL
  #include "scan_studio/viewer_common/opengl/loader.hpp"
  #include "scan_studio/viewer_common/opengl/util.hpp"
#endif

#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"
#include "scan_studio/viewer_common/xrvideo/opengl/opengl_xrvideo.hpp"
#include "scan_studio/viewer_common/render_state.hpp"
#include "scan_studio/viewer_common/timing.hpp"

// resources
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(TARGET_OS_IOS)
  #include "resources/icons/scannedreality_icon_bold.avif.h"
#endif

namespace scan_studio {

#ifndef __ANDROID__
bool ShowDesktopViewer(
    RendererType rendererType, int defaultWindowWidth, int defaultWindowHeight,
    bool preReadCompleteFile, bool cacheAllFrames,
    const char* videoPath, const char* videoTitle,
    bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks,
    bool verboseDecoding, bool useVulkanDebugLayers) {
  // Setup SDL
  #ifdef __APPLE__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  #endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    LOG(ERROR) << "Error initializing SDL: " << SDL_GetError();
    // TODO: Show message box to inform the user (also in similar conditions)
    return false;
  }
  
  // Create render window
  #ifdef __EMSCRIPTEN__
    const string windowTitle = string(videoTitle) + " - ScannedReality";
  #else
    const string windowTitle = "ScannedReality Viewer";
  #endif
  const bool defaultWindowMaximized = false;
  
  shared_ptr<RenderWindowSDL> renderWindow;
  
  if (rendererType == RendererType::Vulkan_1_0) {
    #ifdef HAVE_VULKAN
    RenderWindowSDLVulkan* renderWindowVulkan = new RenderWindowSDLVulkan();
    renderWindow.reset(renderWindowVulkan);
    
    renderWindowVulkan->SetRequestedVulkanVersion(VK_API_VERSION_1_0);
    renderWindowVulkan->SetEnableDebugLayers(useVulkanDebugLayers);
    renderWindowVulkan->SetUseTransientDefaultCommandBuffers(true);
    #else
    (void) useVulkanDebugLayers;
    LOG(ERROR) << "The application was compiled without Vulkan support.";
    return false;
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    RenderWindowSDLMetal* renderWindowMetal = new RenderWindowSDLMetal();
    renderWindow.reset(renderWindowMetal);
    #else
    LOG(ERROR) << "The application was compiled without Metal support.";
    return false;
    #endif
  } else {
    RenderWindowSDLOpenGL* renderWindowOpenGL = new RenderWindowSDLOpenGL();
    renderWindow.reset(renderWindowOpenGL);
    
    if (rendererType == RendererType::OpenGL_ES_3_0) {
      renderWindowOpenGL->SetRequestedOpenGLVersion(3, 0, true);
    } else if (rendererType == RendererType::OpenGL_4_1) {
      renderWindowOpenGL->SetRequestedOpenGLVersion(4, 1, false);
    } else {
      LOG(ERROR) << "rendererType not handled: " << static_cast<int>(rendererType);
    }
  }
  
  RenderCallbacks* callbacks = new RenderCallbacks(
      rendererType, preReadCompleteFile, cacheAllFrames,
      renderWindow.get(), videoPath, videoTitle,
      showMouseControlsHelp, showTouchControlsHelp, showTermsAndPrivacyLinks, verboseDecoding);
  
  if (!renderWindow->Initialize(
      windowTitle.c_str(), defaultWindowWidth, defaultWindowHeight, defaultWindowMaximized ? RenderWindowSDL::WindowState::Maximized : RenderWindowSDL::WindowState::Default,
      shared_ptr<WindowCallbacks>(callbacks))) {
    LOG(ERROR) << "Failed to create render window";
    return false;
  }
  
  #if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(TARGET_OS_IOS)
    renderWindow->SetIconFromAVIFResource(scannedreality_icon_bold_avif.data(), scannedreality_icon_bold_avif.size());
  #endif
  
  // Main loop
  // LOG(1) << "Main thread: Entering main loop ...";
  renderWindow->Exec();
  LOG(1) << "Main thread: Main loop exited";
  
  LOG(1) << "Main thread: SDL_Quit()";
  SDL_Quit();
  
  return true;
}
#endif


RenderCallbacks::RenderCallbacks(
    RendererType rendererType,
    bool preReadCompleteFile, bool cacheAllFrames, RenderWindowSDL* renderWindow,
    const char* videoPath, const char* videoTitle,
    bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks,
    bool verboseDecoding)
    : touchGestureDetector(&touchViewController),
      renderWindow(renderWindow),
      rendererType(rendererType),
      preReadCompleteFile(preReadCompleteFile),
      videoPath(videoPath),
      videoTitle(videoTitle),
      showMouseControlsHelp(showMouseControlsHelp),
      showTouchControlsHelp(showTouchControlsHelp),
      showTermsAndPrivacyLinks(showTermsAndPrivacyLinks),
      verboseDecoding(verboseDecoding),
      commonLogic(rendererType, cacheAllFrames) {
  mouseViewController.Initialize(&view);
}

#ifdef __ANDROID__
void RenderCallbacks::SetEGLConfig(void* config) {
  eglConfig = config;
}
#endif

void RenderCallbacks::SetDPI(float xdpi, float ydpi) {
  this->xdpi = xdpi;
  this->ydpi = ydpi;
}

void RenderCallbacks::PreInitialize(
    VulkanPhysicalDevice* physical_device,
    VkSampleCountFlags* msaaSamples,
    VkPhysicalDeviceFeatures* /*features_to_enable*/,
    vector<string>* /*deviceExtensions*/,
    const void** /*deviceCreateInfoPNext*/) {
#ifndef HAVE_VULKAN
  (void) physical_device;
  (void) msaaSamples;
#else
  // Choose the maximum MSAA sample count that is usable, but at most 16 since I think that higher values do not have a good cost/gain tradeoff.
  // Not sure whether we can rely on the values of the VK_SAMPLE_COUNT_X_BIT enum values, thus we cannot do a simple max().
  const VkSampleCountFlagBits desiredSampleCount = VK_SAMPLE_COUNT_16_BIT;
  const VkSampleCountFlagBits maxUsableSampleCount = physical_device->QueryMaxUsableSampleCount();
  
  std::array<VkSampleCountFlagBits, 6> sampleCountsToCheck = {
      VK_SAMPLE_COUNT_64_BIT,
      VK_SAMPLE_COUNT_32_BIT,
      VK_SAMPLE_COUNT_16_BIT,
      VK_SAMPLE_COUNT_8_BIT,
      VK_SAMPLE_COUNT_4_BIT,
      VK_SAMPLE_COUNT_2_BIT
  };
  
  bool countIsUsable = false;
  bool countIsDesired = false;
  
  *msaaSamples = VK_SAMPLE_COUNT_1_BIT;
  
  for (VkSampleCountFlagBits count : sampleCountsToCheck) {
    if (count == maxUsableSampleCount) {
      countIsUsable = true;
    }
    if (count == desiredSampleCount) {
      countIsDesired = true;
    }
    
    if (countIsUsable && countIsDesired) {
      *msaaSamples = count;
      break;
    }
  }
#endif
}

bool RenderCallbacks::Initialize() {
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &RenderCallbacks::InitializeImplStatic, reinterpret_cast<int>(this));
  #else
    InitializeImpl();
  #endif
  
  return true;  // TODO: Check for initialization errors
}

void RenderCallbacks::Deinitialize() {
  // Note that we do not generally proxy commonLogic.Deinitialize() to the main thread for emscripten.
  // This is because commonLogic.Deinitialize() calls xrVideo->Destroy(), which calls join() on the XRVideo loading threads.
  // Since we should absolutely avoid blocking the main thread in emscripten, we thus do the main thread proxying with a finer
  // granularity there.
  
  commonLogic.Deinitialize();
  
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &RenderCallbacks::DeinitializeImplStatic, reinterpret_cast<int>(this));
  #else
    DeinitializeImpl();
  #endif
  
  #ifndef __ANDROID__
    SDL_FreeCursor(defaultCursor);
    SDL_FreeCursor(handCursor);
  #endif
}

bool RenderCallbacks::InitializeSurfaceDependent() {
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &RenderCallbacks::InitializeSurfaceDependentImplStatic, reinterpret_cast<int>(this));
    return true;  // TODO: Check for initialization errors
  #else
    return InitializeSurfaceDependentImpl();
  #endif
}

void RenderCallbacks::DeinitializeSurfaceDependent() {
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, &RenderCallbacks::DeinitializeSurfaceDependentImplStatic, reinterpret_cast<int>(this));
  #else
    DeinitializeSurfaceDependentImpl();
  #endif
}

void RenderCallbacks::Resize(int width, int height) {
  this->width = width;
  this->height = height;
}

void RenderCallbacks::Render(int imageIndex) {
  #ifdef __EMSCRIPTEN__  // See the web viewer doc on why we must proxy all OpenGL function calls to the main thread
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VII, &RenderCallbacks::RenderImplStatic, reinterpret_cast<int>(this), imageIndex);
  #else
    RenderImpl(imageIndex);
  #endif
}

void RenderCallbacks::MouseDown(MouseButton button, int x, int y, int /*clickCount*/) {
  if (button == MouseButton::kLeft &&
      flatscreenUI.TouchOrClickDown(x, y)) {
    return;
  }
  
  autoViewAnimation = false;
  
  mouseViewController.MouseDown(button, x, y);
}

void RenderCallbacks::MouseMove(int x, int y) {
  #ifndef __ANDROID__
  SDL_Cursor* requestedCursor = flatscreenUI.Hover(x, y) ? handCursor : defaultCursor;
  if (requestedCursor != currentCursor) {
    SDL_SetCursor(requestedCursor);
    currentCursor = requestedCursor;
  }
  #endif
  
  if (flatscreenUI.TouchOrClickMove(x, y)) {
    return;
  }
  
  mouseViewController.MouseMove(x, y);
}

void RenderCallbacks::MouseUp(MouseButton button, int x, int y, int /*clickCount*/) {
  if (flatscreenUI.TouchOrClickUp(x, y)) {
    return;
  }
  
  mouseViewController.MouseUp(button, x, y);
}

void RenderCallbacks::WheelRotated(float degrees, Modifier modifiers) {
  // With emscripten, we got +- 114 degrees here, while with desktop SDL, we got +- 1.
  // So, we just use the sign of the value for now to avoid issues with too large steps.
  degrees = (degrees > 0) ? 1 : -1;
  
  if (flatscreenUI.WheelRotated(degrees)) {
    return;
  }
  
  mouseViewController.WheelRotated(degrees, modifiers);
}

void RenderCallbacks::FingerDown(s64 finger_id, float x, float y) {
  if (flatscreenUI.TouchOrClickDown(x, y)) {
    uiInteractionFingerIDs.push_back(finger_id);
    return;
  }
  
  autoViewAnimation = false;
  
  touchGestureDetector.FingerDown(finger_id, x, y);
}

void RenderCallbacks::FingerMove(s64 finger_id, float x, float y) {
  for (s64 id : uiInteractionFingerIDs) {
    if (finger_id == id) {
      flatscreenUI.TouchOrClickMove(x, y);
      return;
    }
  }
  
  touchGestureDetector.FingerMove(finger_id, x, y);
}

void RenderCallbacks::FingerUp(s64 finger_id, float x, float y) {
  for (int i = 0; i < uiInteractionFingerIDs.size(); ++ i) {
    if (finger_id == uiInteractionFingerIDs[i]) {
      uiInteractionFingerIDs.erase(uiInteractionFingerIDs.begin() + i);
      flatscreenUI.TouchOrClickUp(x, y);
      return;
    }
  }
  
  touchGestureDetector.FingerUp(finger_id, x, y);
}

void RenderCallbacks::KeyPressed(int /*key*/, Modifier /*modifiers*/) {
  // if (key == 'p') {
  //   if (commonLogic.GetXRVideo()) {
  //     commonLogic.GetXRVideo()->DebugPrintCacheHealth();
  //   }
  // }
  
  // This was for creating the "bg_reconstruction_angles" image for the website:
  // if (key == 'd') {
  //   view.yaw += 20.f / 180.f * M_PI;
  // }
  
  // This was for the "featurette_cat" image for the website:
  // if (key == 's') {
  //   useSurfaceNormalShading = !useSurfaceNormalShading;
  // }
  
//   if (key == 'v') {
//     auto& playbackState = commonLogic.GetXRVideo()->GetPlaybackState();
//     playbackState.Lock();
//     LOG(INFO) << "Current playback time: " << playbackState.GetPlaybackTime();
//     playbackState.Unlock();
//     LOG(INFO) << "Current view: " << view.lookAt.x() << "," << view.lookAt.y() << "," << view.lookAt.z() << "," << view.radius << "," << view.yaw << "," << view.pitch;
//   }
}

void RenderCallbacks::InitializeImpl() {
  #ifndef __ANDROID__
    // Get cursors
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    
    currentCursor = defaultCursor;
    
    // Get the display's DPI and use that to initialize the touchViewController.
    #ifdef __EMSCRIPTEN__
      // At the time of writing, unfortunately there seems to be no proper way to determine display DPI
      // with Javascript running in a browser. SDL_GetDisplayDPI() fails with emscripten, with
      // SDL_GetError() saying that it is not supported.
      //
      // Thus, we resort to a bad approximation using emscripten_get_device_pixel_ratio() ...
      // Quoting https://developer.mozilla.org/en-US/docs/Web/API/Window/devicePixelRatio :
      // "A value of 1 indicates a classic 96 DPI (76 DPI on some platforms) display,
      //  while a value of 2 is expected for HiDPI/Retina displays. Other values may be
      //  returned as well in the case of unusually low resolution displays or, more often,
      //  when a screen has a higher pixel depth than double the standard resolution of 96 or 76 DPI."
      const double windowDevicePixelRatio = emscripten_get_device_pixel_ratio();
      xdpi = windowDevicePixelRatio * 96;
      ydpi = windowDevicePixelRatio * 96;
    #else
      // Get the window's display index (or clamp up to 0 in case of a negative return value due to an error).
      SDL_Window* sdlWindow = renderWindow->GetWindow();
      const int windowDisplayIndex = SDL_GetWindowDisplayIndex(sdlWindow);
      if (windowDisplayIndex < 0) {
        LOG(ERROR) << "SDL_GetWindowDisplayIndex() failed, SDL_GetError() returns: " << SDL_GetError();
      }
      
      if (SDL_GetDisplayDPI(/*displayIndex*/ sdlWindow ? max(0, windowDisplayIndex) : 0, nullptr, &xdpi, &ydpi) < 0) {
        LOG(ERROR) << "SDL_GetDisplayDPI() failed, SDL_GetError() returns: " << SDL_GetError();
        xdpi = 443;
        ydpi = 443;
      }
    #endif
  #endif
  
  LOG(1) << "xdpi: " << xdpi << ", ydpi: " << ydpi;
  touchViewController.Initialize(xdpi, ydpi, &view);
  
  // Initialize common logic.
  commonLogic.Initialize();
}

void RenderCallbacks::InitializeImplStatic(int thisInt) {
  #ifdef __EMSCRIPTEN__
    RenderCallbacks* thisPtr = reinterpret_cast<RenderCallbacks*>(thisInt);
    thisPtr->InitializeImpl();
  #else
    (void) thisInt;
  #endif
}

void RenderCallbacks::DeinitializeImpl() {
  flatscreenUI.Deinitialize();
}

void RenderCallbacks::DeinitializeImplStatic(int thisInt) {
  #ifdef __EMSCRIPTEN__
    RenderCallbacks* thisPtr = reinterpret_cast<RenderCallbacks*>(thisInt);
    thisPtr->DeinitializeImpl();
  #else
    (void) thisInt;
  #endif
}

bool RenderCallbacks::InitializeSurfaceDependentImpl() {
  if (commonLogic.GetXRVideo()) {
    // TODO: We should check here whether we must recreate some resources
    return true;
  }
  
  #ifdef HAVE_OPENGL
    if (rendererType == RendererType::OpenGL_4_1) {
      // TODO: Use VAOs properly
      GLuint vao;
      gl.glGenVertexArrays(1, &vao);
      gl.glBindVertexArray(vao);
    }
  #endif
  
  #ifdef HAVE_VULKAN
    RenderWindowSDLVulkan* vulkanRenderWindow = nullptr;
    if (rendererType == RendererType::Vulkan_1_0) {
      vulkanRenderWindow = dynamic_cast<RenderWindowSDLVulkan*>(renderWindow);
      if (!vulkanRenderWindow) { return false; }
      // Some Vulkan objects are only initialized here (not in Initialize() yet).
      commonLogic.InitializeVulkan(
          vulkanRenderWindow->GetMaxFramesInFlight(),
          vulkanRenderWindow->GetDefaultRenderPass()->Attachments()[0].samples,  // since we always render to attachment 0, we can get the sample count from there
          *vulkanRenderWindow->GetDefaultRenderPass(),
          &vulkanRenderWindow->GetDevice());
    }
  #endif
  
  #ifdef __APPLE__
    if (rendererType == RendererType::Metal) {
      RenderWindowSDLMetal* metalRenderWindow = dynamic_cast<RenderWindowSDLMetal*>(renderWindow);
      if (!metalRenderWindow) { return false; }
      commonLogic.InitializeMetal(
          metalRenderWindow->GetMaxFramesInFlight(),
          metalRenderWindow->GetLibraryCache(),
          metalRenderWindow->GetRenderPassDesc(),
          metalRenderWindow->GetDevice());
    }
  #endif
  
  unique_ptr<RenderState> renderState;
  if (rendererType == RendererType::Vulkan_1_0) {
    #ifdef HAVE_VULKAN
      renderState.reset(new VulkanRenderState(
          &vulkanRenderWindow->GetDevice(),
          vulkanRenderWindow->GetDefaultRenderPass(),
          vulkanRenderWindow->GetMaxFramesInFlight(),
          /*cmdBuf*/ nullptr,
          /*frameInFlightIndex*/ 0));
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    RenderWindowSDLMetal* metalRenderWindow = dynamic_cast<RenderWindowSDLMetal*>(renderWindow);
    renderState.reset(new MetalRenderState(
        metalRenderWindow->GetDevice(),
        metalRenderWindow->GetLibraryCache(),
        metalRenderWindow->GetRenderPassDesc(),
        metalRenderWindow->GetMaxFramesInFlight()));
    #endif
  } else {
    renderState.reset(new OpenGLRenderState());
  }
  
  #ifdef __ANDROID__
    void* openGLContextCreationUserPtr = eglConfig;
    (void) renderWindow;
  #else
    void* openGLContextCreationUserPtr = renderWindow->GetWindow();
  #endif
  
  // The desktop viewer only renders the XRVideo once per frame, hence the viewCount is 1.
  if (!commonLogic.InitializeGraphicObjects(/*viewCount*/ 1, verboseDecoding, openGLContextCreationUserPtr)) {
    LOG(ERROR) << "Failed to initialize graphic objects!";
    // TODO: Report error to user properly
    return false;
  }
  
  if (!commonLogic.OpenFile(preReadCompleteFile, videoPath)) {
    LOG(ERROR) << "Failed to open video file!";
    // TODO: Report error to user properly
    return false;
  }
  
  // Initialize the UI
  if (!flatscreenUI.Initialize(
      commonLogic.GetXRVideo().get(), commonLogic.GetAudio().get(), renderState.get(),
      videoTitle.empty() ? fs::path(videoPath).filename().string() : videoTitle, showMouseControlsHelp, showTouchControlsHelp, showTermsAndPrivacyLinks)) {
    // TODO: Handle the error properly
    return false;
  }
  
  lastFrameTime = Clock::now();
  return true;
}

void RenderCallbacks::InitializeSurfaceDependentImplStatic(int thisInt) {
  #ifdef __EMSCRIPTEN__
    RenderCallbacks* thisPtr = reinterpret_cast<RenderCallbacks*>(thisInt);
    thisPtr->InitializeSurfaceDependentImpl();
  #else
    (void) thisInt;
  #endif
}

void RenderCallbacks::DeinitializeSurfaceDependentImpl() {}

void RenderCallbacks::DeinitializeSurfaceDependentImplStatic(int thisInt) {
  #ifdef __EMSCRIPTEN__
    RenderCallbacks* thisPtr = reinterpret_cast<RenderCallbacks*>(thisInt);
    thisPtr->DeinitializeSurfaceDependentImpl();
  #else
    (void) thisInt;
  #endif
}

void RenderCallbacks::RenderImpl(int imageIndex) {
  (void) imageIndex;  // this is unused if not HAVE_VULKAN
  
  // Asynchronous initializations after XRVideo loading
  if (!initialViewInitialized && commonLogic.GetXRVideo()->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
    // If the XRVideo has view metadata, initialize the view settings with it.
    if (commonLogic.GetXRVideo()->HasMetadata()) {
      const XRVideoMetadata& metadata = commonLogic.GetXRVideo()->GetMetadata();
      view.lookAt = Eigen::Vector3f(metadata.lookAtX, metadata.lookAtY, metadata.lookAtZ);
      view.radius = metadata.radius;
      view.yaw = metadata.yaw;
      view.pitch = metadata.pitch;
    }
    initialView = view;
    initialViewInitialized = true;
  }
  
  // Update the application time.
  // TODO: Like the OpenXR version, we should ideally use the predicted display time for rendering, not the current time during frame processing.
  const TimePoint renderStartTime = Clock::now();
  const s64 nanosecondsSinceLastFrame = NanosecondsFromTo(lastFrameTime, renderStartTime);
  applicationTimeNanoseconds += nanosecondsSinceLastFrame;
  lastFrameTime = renderStartTime;
  
  // Set the 3D camera parameters
  if (autoViewAnimation && initialViewInitialized && !commonLogic.GetXRVideo()->BufferingIndicatorShouldBeShown()) {
    viewAnimationTimeNanoseconds += nanosecondsSinceLastFrame;
    view.yaw = initialView.yaw + 0.5f * sinf(NanosecondsToSeconds(viewAnimationTimeNanoseconds));
    
    // For recording the teaser video for the website:
    // view.yaw = initialView.yaw - 1.f * viewAnimationTime;
  }
  
  const double verticalFoV = M_PI / 180.f * 40.0f;
  const double aspect = width / (1.0f * height);
  
  const Eigen::Vector3f eye = view.ComputeEyePosition();
  const Eigen::Vector3f up(0, 1, 0);
  
  const Eigen::Matrix4f renderCamera_tr_global = LookAtMatrix(eye, view.lookAt, up);  // this would be called "view" in Computer Graphics
  Eigen::Matrix4f projection;
  if (IsOpenGLRendererType(rendererType)) {
    #ifdef HAVE_OPENGL
      projection = PerspectiveMatrixOpenGL(verticalFoV, aspect, minDepth, maxDepth);
    #endif
  } else {
    projection = PerspectiveMatrix(verticalFoV, aspect, minDepth, maxDepth);
  }
  const Eigen::Matrix4f renderCameraProj_tr_global = projection * renderCamera_tr_global;  // this would be called "viewProjection" in Computer Graphics
  
  // Render content
  unique_ptr<RenderState> renderState;
  
  if (rendererType == RendererType::Vulkan_1_0) {
    #ifdef HAVE_VULKAN
    RenderWindowSDLVulkan* vulkanRenderWindow = dynamic_cast<RenderWindowSDLVulkan*>(renderWindow);
    
    VulkanRenderState* vulkanRenderState = new VulkanRenderState(
        &vulkanRenderWindow->GetDevice(),
        vulkanRenderWindow->GetDefaultRenderPass(),
        vulkanRenderWindow->GetMaxFramesInFlight(),
        &vulkanRenderWindow->GetCurrentCommandBuffer(),
        vulkanRenderWindow->GetCurrentFrameInFlight());
    renderState.reset(vulkanRenderState);
    
    if (!VulkanCheckResult(vulkanRenderState->device->Api().vkResetCommandBuffer(*vulkanRenderState->cmdBuf, /*flags*/ 0))) {
      LOG(ERROR) << "Failed to reset a command buffer";
      return;
    }
    if (!vulkanRenderState->cmdBuf->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) {
      LOG(ERROR) << "Failed to Begin() a command buffer";
      return;
    }
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    RenderWindowSDLMetal* metalRenderWindow = dynamic_cast<RenderWindowSDLMetal*>(renderWindow);
    renderState.reset(new MetalRenderState(
        metalRenderWindow->GetDevice(),
        metalRenderWindow->GetLibraryCache(),
        metalRenderWindow->GetRenderPassDesc(),
        metalRenderWindow->GetMaxFramesInFlight(),
        metalRenderWindow->GetCurrentCommandBuffer()));
    #endif
  } else {
    renderState.reset(new OpenGLRenderState());
  }
  
  commonLogic.PrepareFrame(applicationTimeNanoseconds, flatscreenUI.IsPaused(), renderState.get());
  commonLogic.PrepareView(/*viewIndex*/ 0, useSurfaceNormalShading, renderState.get());
  
  float playbackAmount;
  bool showRepeatButton;
  
  if (commonLogic.GetXRVideo()->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
    PlaybackState& playbackState = commonLogic.GetXRVideo()->GetPlaybackState();
    playbackState.Lock();
    const s64 playbackTime = playbackState.GetPlaybackTime();
    PlaybackMode playbackMode = playbackState.GetPlaybackMode();
    playbackState.Unlock();
    
    const FrameIndex& frameIndex = commonLogic.GetXRVideo()->Index();
    playbackAmount = (playbackTime - frameIndex.GetVideoStartTimestamp()) / (1.0 * (frameIndex.GetVideoEndTimestamp() - frameIndex.GetVideoStartTimestamp()));
    showRepeatButton =
        playbackMode == PlaybackMode::SingleShot &&
        playbackTime == frameIndex.GetVideoEndTimestamp();
  } else {
    playbackAmount = 0;
    showRepeatButton = false;
  }
  
  flatscreenUI.Update(
      nanosecondsSinceLastFrame, playbackAmount,
      showRepeatButton, /*showInfoButton*/ true, /*showBackButton*/ false, commonLogic.GetXRVideo()->BufferingIndicatorShouldBeShown(), commonLogic.GetXRVideo()->GetBufferingProgressPercent(),
      width, height, xdpi, ydpi, renderState.get());
  
  Eigen::Vector3f backgroundColor(0.1f, 0.1f, 0.1f);
  
  if (rendererType == RendererType::Vulkan_1_0) {
    #ifdef HAVE_VULKAN
    VulkanRenderState* vulkanRenderState = reinterpret_cast<VulkanRenderState*>(renderState.get());
    RenderWindowSDLVulkan* vulkanRenderWindow = dynamic_cast<RenderWindowSDLVulkan*>(renderWindow);
    
    // Convert the sRGB background color to linear color space
    // (as it will get converted back to sRGB when written to the render target).
    backgroundColor = SRGBToLinear(backgroundColor);
    
    VkRenderPassBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = *vulkanRenderWindow->GetDefaultRenderPass();
    info.framebuffer = vulkanRenderWindow->GetSwapChainFramebuffers()->at(imageIndex);
    info.renderArea.extent = vulkanRenderWindow->GetSwapChainExtent();
    VkClearValue clear_values[2];
    clear_values[0].color.float32[0] = backgroundColor[0];
    clear_values[0].color.float32[1] = backgroundColor[1];
    clear_values[0].color.float32[2] = backgroundColor[2];
    clear_values[0].color.float32[3] = 1.f;
    clear_values[1].depthStencil.depth = 1.f;
    clear_values[1].depthStencil.stencil = 0;  // irrelevant
    info.clearValueCount = 2;
    info.pClearValues = clear_values;
    vulkanRenderState->device->Api().vkCmdBeginRenderPass(*vulkanRenderState->cmdBuf, &info, VK_SUBPASS_CONTENTS_INLINE);
    
    VulkanCmdSetViewportAndScissor(*vulkanRenderState->cmdBuf, *vulkanRenderState->device, 0, 0, vulkanRenderWindow->GetSwapChainExtent().width, vulkanRenderWindow->GetSwapChainExtent().height);
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    MetalRenderState* metalRenderState = reinterpret_cast<MetalRenderState*>(renderState.get());
    RenderWindowSDLMetal* metalRenderWindow = dynamic_cast<RenderWindowSDLMetal*>(renderWindow);
    
    MTL::RenderPassDescriptor* renderPass = metalRenderWindow->GetRenderPass();
    auto colorAttachment = renderPass->colorAttachments()->object(0);
    colorAttachment->setClearColor(MTL::ClearColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], 1.0));
    metalRenderState->renderCmdEncoder = metalRenderState->cmdBuf->renderCommandEncoder(renderPass);
    
    metalRenderState->renderCmdEncoder->setViewport(MTL::Viewport{
        /*originX*/ 0.0f, /*originY*/ 0.0f,
        static_cast<double>(width), static_cast<double>(height),
        /*znear*/ 0.0f, /*zfar*/ 1.0f
    });
    #endif
  } else if (IsOpenGLRendererType(rendererType)) {
    #ifdef HAVE_OPENGL
      CHECK_OPENGL_NO_ERROR();
      gl.glViewport(0, 0, width, height);
      
      gl.glClearColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], 1.f);
      gl.glEnable(GL_DEPTH_TEST);
      gl.glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
      CHECK_OPENGL_NO_ERROR();
      
      gl.glEnable(GL_CULL_FACE);
      // GL_CCW is the initial value, so no need to change it.
      // gl.glFrontFace(GL_CCW);
      CHECK_OPENGL_NO_ERROR();
    #endif
  }
  
  commonLogic.SetModelViewProjection(/*viewIndex*/ 0, renderCamera_tr_global.data(), renderCameraProj_tr_global.data());
  commonLogic.RenderView(renderState.get());
  
  commonLogic.EndFrame();
  
  flatscreenUI.RenderView(renderState.get());
  
  if (rendererType == RendererType::Vulkan_1_0) {
    #ifdef HAVE_VULKAN
    VulkanRenderState* vulkanRenderState = reinterpret_cast<VulkanRenderState*>(renderState.get());
    const auto& api = vulkanRenderState->device->Api();
    api.vkCmdEndRenderPass(*vulkanRenderState->cmdBuf);
    api.vkEndCommandBuffer(*vulkanRenderState->cmdBuf);
    #endif
  } else if (rendererType == RendererType::Metal) {
    #ifdef __APPLE__
    MetalRenderState* metalRenderState = reinterpret_cast<MetalRenderState*>(renderState.get());
    metalRenderState->renderCmdEncoder->endEncoding();
    #endif
  }
  
  constexpr bool kLogRenderCPUTime = false;
  if (kLogRenderCPUTime) {
    const TimePoint renderEndTime = Clock::now();
    LOG(1) << "Main thread: Render() took " << MillisecondsFromTo(renderStartTime, renderEndTime) << " ms";
  }
  
  constexpr bool kPrintView = false;
  if (kPrintView) {
    LOG(1) << "View: " <<
        view.lookAt.x() << " " <<
        view.lookAt.y() << " " <<
        view.lookAt.z() << " " <<
        view.radius << " " <<
        view.yaw << " " <<
        view.pitch;
  }
}

void RenderCallbacks::RenderImplStatic(int thisInt, int imageIndex) {
  #ifdef __EMSCRIPTEN__
    RenderCallbacks* thisPtr = reinterpret_cast<RenderCallbacks*>(thisInt);
    thisPtr->RenderImpl(imageIndex);
  #else
    (void) thisInt;
    (void) imageIndex;
  #endif
}

}
