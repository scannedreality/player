#pragma once
#ifdef __APPLE__

#include <condition_variable>
#include <mutex>
#include <ostream>
#include <vector>

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <SDL_metal.h>

#include <libvis/util/window_callbacks.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/metal/library_cache.hpp"

#include "scan_studio/viewer_common/platform/render_window_sdl.hpp"

struct SDL_Window;

namespace CA {
  class MetalDrawable;
  class MetalLayer;
}

namespace vis {

/// Groups data that is required to set up render pipelines targeting the render window's color and depth attachments.
/// We cannot conveniently use the MTL::RenderPassDescriptor itself, since it stores the attachments' pixel formats
/// only by way of storing the attached textures, and at the time where we want to query that information,
/// these textures may not be attached yet (the first color texture will only be attached once rendering for the first frame starts).
struct MetalRenderPassDescriptor {
  inline bool operator== (const MetalRenderPassDescriptor& other) const {
    return rasterSampleCount == other.rasterSampleCount &&
           colorFormats == other.colorFormats &&
           depthFormat == other.depthFormat &&
           stencilFormat == other.stencilFormat;
  }
  
  friend std::ostream& operator<< (std::ostream& stream, const MetalRenderPassDescriptor& desc);
  
  u32 rasterSampleCount;
  vector<MTL::PixelFormat> colorFormats;
  MTL::PixelFormat depthFormat;
  MTL::PixelFormat stencilFormat;
};

inline std::ostream& operator<< (std::ostream& stream, const MetalRenderPassDescriptor& desc) {
  stream << "Raster sample count: " << static_cast<int>(desc.rasterSampleCount) << "\n";
  for (const MTL::PixelFormat& format : desc.colorFormats) {
    stream << "Color format: " << static_cast<int>(format) << "\n";
  }
  stream << "Depth format: " << static_cast<int>(desc.depthFormat) << "\n";
  stream << "Stencil format: " << static_cast<int>(desc.stencilFormat) << "\n";
  return stream;
}

class RenderWindowSDLMetal : public scan_studio::RenderWindowSDL {
 public:
  RenderWindowSDLMetal();
  ~RenderWindowSDLMetal();
  
  virtual void Deinitialize() override;
  
  /// Returns the window's SDL_MetalView, which can be cast directly to an NSView (on macOS) or UIView (on iOS/tvOS).
  inline SDL_MetalView& GetView() { return sdlView; }
  
  inline const MTL::Device* GetDevice() const { return device; }
  inline MTL::Device* GetDevice() { return device; }
  
  inline const scan_studio::MetalLibraryCache* GetLibraryCache() const { return &libraryCache; }
  inline scan_studio::MetalLibraryCache* GetLibraryCache() { return &libraryCache; }
  
  // inline const CA::MetalLayer* GetSwapChain() const { return swapChain; }
  // inline CA::MetalLayer* GetSwapChain() { return swapChain; }
  
  inline const MTL::RenderPassDescriptor* GetRenderPass() const { return renderPass.get(); }
  inline MTL::RenderPassDescriptor* GetRenderPass() { return renderPass.get(); }
  
  inline const MetalRenderPassDescriptor* GetRenderPassDesc() const { return &renderPassDesc; }
  inline MetalRenderPassDescriptor* GetRenderPassDesc() { return &renderPassDesc; }
  
  inline int GetMaxFramesInFlight() const { return maxFramesInFlight; }
  
  inline const MTL::CommandQueue* GetCommandQueue() const { return commandQueue; }
  inline MTL::CommandQueue* GetCommandQueue() { return commandQueue; }
  
  inline const CA::MetalDrawable* GetCurrentDrawable() const { return currentDrawable; }
  inline CA::MetalDrawable* GetCurrentDrawable() { return currentDrawable; }
  
  inline const MTL::CommandBuffer* GetCurrentCommandBuffer() const { return currentCmdBuf; }
  inline MTL::CommandBuffer* GetCurrentCommandBuffer() { return currentCmdBuf; }
  
 protected:
  virtual bool InitializeImpl(const char *title, int width, int height, WindowState windowState) override;
  virtual void GetDrawableSize(int *width, int *height) override;
  virtual void Resize(int width, int height) override;
  virtual void Render() override;
  
  bool CreateSurfaceDependentObjects();
  void DestroySurfaceDependentObjects();
  
 private:
  MTL::Device* device = nullptr;
  NS::AutoreleasePool* appAutoreleasePool = nullptr;
  
  scan_studio::MetalLibraryCache libraryCache;
  
  SDL_MetalView sdlView = nullptr;
  CA::MetalLayer* swapChain = nullptr;
  NS::SharedPtr<MTL::Texture> depthTexture;
  MetalRenderPassDescriptor renderPassDesc;
  NS::SharedPtr<MTL::RenderPassDescriptor> renderPass;
  int maxFramesInFlight;
  dispatch_semaphore_t inFlightSemaphore = nullptr;  // TODO: Can this object be released? Does it use some Objective-C mechanism for garbage collection?
  
  mutex currentFramesInFlightMutex;
  int currentFramesInFlight = 0;
  condition_variable currentFramesInFlightDecreasedCondition;
  
  MTL::CommandQueue* commandQueue = nullptr;
  
  CA::MetalDrawable* currentDrawable = nullptr;
  MTL::CommandBuffer* currentCmdBuf = nullptr;
};

}

#endif
