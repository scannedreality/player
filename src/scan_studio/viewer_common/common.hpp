#pragma once

#include <memory>

#if defined(_WIN32) || defined(__APPLE__)
  #include <SDL_opengl.h>
#else
  #include <GLES2/gl2.h>
#endif

#include <libvis/io/filesystem.h>

#include <libvis/vulkan/libvis.h>
#ifdef HAVE_VULKAN
#include <libvis/vulkan/command_pool.h>
#include <libvis/vulkan/device.h>
#include <libvis/vulkan/render_pass.h>
#endif

#include "scan_studio/viewer_common/timing.hpp"

struct SDL_Window;

namespace MTL {
class Device;
}

namespace vis {
class InputStream;
struct MetalRenderPassDescriptor;
}

namespace scan_studio {
using namespace vis;

class MetalLibraryCache;
struct RenderState;
class SDLAudio;
class XRVideo;
class XRVideoCommonResources;
class XRVideoRenderLock;

enum class RendererType {
  Metal,
  
  /// OpenGL 4.1 appears to be the newest OpenGL version supported on MacOS, this is why we consider it here
  OpenGL_4_1,
  
  OpenGL_ES_3_0,
  
  Vulkan_1_0,
  
  Vulkan_1_0_OpenXR_1_0
};

inline bool IsOpenGLRendererType(RendererType type) {
  return type == RendererType::OpenGL_4_1 ||
         type == RendererType::OpenGL_ES_3_0;
}

/// Common application logic used by both the desktop and OpenXR display mode,
/// i.e., the basic XRVideo display, without any input or UI handling.
class ViewerCommon {
 public:
  /// If cacheAllFrames is true, all decoded frames are cached in video memory.
  /// This is useful if we know that decoding will be slow, but the video file is
  /// small enough to fit into memory.
  ViewerCommon(RendererType rendererType, bool cacheAllFrames);
  ~ViewerCommon();
  
  void Initialize();
  void Deinitialize();
  
  #ifdef HAVE_VULKAN
  /// If the Vulkan renderer is used, this function must be called before
  /// calling InitializeGraphicObjects() to let the class know about the relevant Vulkan objects.
  void InitializeVulkan(int maxFramesInFlightCount, VkSampleCountFlagBits msaaSamples, VkRenderPass renderPass, VulkanDevice* device);
  #endif
  
  #ifdef __APPLE__
  /// If the Metal renderer is used, this function must be called before
  /// calling InitializeGraphicObjects() to let the class know about the relevant Metal objects.
  void InitializeMetal(int maxFramesInFlightCount, MetalLibraryCache* libraryCache, MetalRenderPassDescriptor* renderPassDesc, MTL::Device* device);
  #endif
  
  bool InitializeGraphicObjects(int viewCount, bool verboseDecoding, void* openGLContextCreationUserPtr);
  
  bool OpenFile(bool preReadCompleteFile, const filesystem::path& videoPath);
  
  /// Must be called once at the start of the frame (for Vulkan: before the render pass is started).
  void PrepareFrame(s64 predictedDisplayTimeNanoseconds, bool paused, RenderState* renderState);
  
  /// Must be called before each view is rendered (for Vulkan: before the render pass is started).
  void PrepareView(int viewIndex, bool useSurfaceNormalShading, RenderState* renderState);
  
  /// Must be called for each view as part of its render pass.
  void RenderView(RenderState* renderState);
  
  /// Must be called at the end of each frame (to destroy the internal render lock).
  void EndFrame();
  
  // TODO: I think ViewerCommon should only take the viewProjection matrix and append the model part itself as needed
  bool SupportsLateModelViewProjectionSetting();
  void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewData, const float* columnMajorModelViewProjectionData);
  
  inline const shared_ptr<XRVideo>& GetXRVideo() const { return xrVideo; }
  inline shared_ptr<XRVideo>& GetXRVideo() { return xrVideo; }
  
  inline const unique_ptr<SDLAudio>& GetAudio() const { return audio; }
  inline unique_ptr<SDLAudio>& GetAudio() { return audio; }
  
  static s64 GetAudioSynchronizedPlaybackDelta(bool paused, s64 elapsedNanoseconds, XRVideo* xrVideo, SDLAudio* audio);
  
 private:
  unique_ptr<XRVideoCommonResources> xrVideoCommonResources;
  shared_ptr<XRVideo> xrVideo;
  shared_ptr<XRVideoRenderLock> xrVideoRenderLock;
  
  unique_ptr<SDLAudio> audio;
  
  bool lastDisplayTimeInitialized = false;
  s64 lastDisplayTimeNanoseconds;
  
  // For OpenGL only
  // bool usingExtension_GL_EXT_sRGB = false;
  
  // For Vulkan only
  #ifdef HAVE_VULKAN
  struct Vulkan {
    VkSampleCountFlagBits msaaSamples;
    VkRenderPass renderPass;  // not owned
    VulkanDevice* device;  // not owned
  } vulkan;
  #endif
  
  // For Metal only
  #ifdef __APPLE__
  struct Metal {
    MetalLibraryCache* libraryCache;  // not owned
    MetalRenderPassDescriptor* renderPassDesc;  // not owned
    MTL::Device* device;  // not owned
  } metal;
  #endif
  
  #if defined(HAVE_VULKAN) || defined(__APPLE__)
  int maxFramesInFlightCount = 0;
  #endif
  
  // Renderer type: OpenGL or Vulkan
  RendererType rendererType;
  
  /// If cacheAllFrames is true, all decoded frames are cached in video memory.
  /// This is useful if we know that decoding will be slow, but the video file is
  /// small enough to fit into memory.
  bool cacheAllFrames;
};

}
