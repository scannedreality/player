#ifdef __APPLE__

// These implementations must come first, otherwise includes of the same headers may
// be included before the implementation macros are defined, which will cause the implementations to be missing.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
// #include <AppKit/AppKit.hpp>
// #include <MetalKit/MetalKit.hpp>

#define CA_PRIVATE_IMPLEMENTATION
#include <QuartzCore/QuartzCore.hpp>

#include "scan_studio/viewer_common/platform/render_window_sdl_metal.hpp"

#include <algorithm>
#include <array>

#include <loguru.hpp>

namespace CA {
  namespace Private {
    namespace Selector {
      _CA_PRIVATE_DEF_SEL(setDisplaySyncEnabled_, "setDisplaySyncEnabled:");
      _CA_PRIVATE_DEF_SEL(maximumDrawableCount, "maximumDrawableCount");
    }
  }
  
  class MetalLayerHelper : public NS::Referencing<MetalLayer> {
   public:
    static void SetDisplaySyncEnabled(CA::MetalLayer* layer, bool enable) {
      NS::Object::sendMessage<void>(layer, _CA_PRIVATE_SEL(setDisplaySyncEnabled_), enable);
    }
    
    static int GetMaximumDrawableCount(CA::MetalLayer* layer) {
      return Object::sendMessage<int>(layer, _CA_PRIVATE_SEL(maximumDrawableCount));
    }
  };
}

namespace vis {

RenderWindowSDLMetal::RenderWindowSDLMetal() {}

RenderWindowSDLMetal::~RenderWindowSDLMetal() {}

void RenderWindowSDLMetal::Deinitialize() {
  // Wait for all command buffers to finish.
  // This helps avoid potential issues with pointed-to memory becoming invalid in command buffer completion handler callbacks.
  {
    unique_lock<mutex> lock(currentFramesInFlightMutex);
    while (currentFramesInFlight > 0) {
      currentFramesInFlightDecreasedCondition.wait(lock);
    }
  }
  
  callbacks_->DeinitializeSurfaceDependent();
  DestroySurfaceDependentObjects();
  callbacks_->Deinitialize();
  callbacks_.reset();
  
  if (sdlView) {
    SDL_Metal_DestroyView(sdlView);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  
  if (commandQueue) {
    commandQueue->release();
    commandQueue = nullptr;
  }
  
  if (device) {
    device->release();
    device = nullptr;
  }
  
  if (appAutoreleasePool) {
    appAutoreleasePool->release();
    appAutoreleasePool = nullptr;
  }
}

bool RenderWindowSDLMetal::InitializeImpl(const char* title, int width, int height, WindowState windowState) {
  shared_ptr<NS::AutoreleasePool> initializationAutoreleasePool(NS::AutoreleasePool::alloc()->init(), [](NS::AutoreleasePool* pool) { pool->release(); });
  
  // Setup window
  window_ = SDL_CreateWindow(
      title,
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height,
      static_cast<SDL_WindowFlags>(SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
          | ((windowState == RenderWindowSDL::WindowState::Maximized) ? SDL_WINDOW_MAXIMIZED : 0)
          | ((windowState == RenderWindowSDL::WindowState::Fullscreen) ? (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS) : 0)
          |
          #ifdef TARGET_OS_IOS
            SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS
          #else
            0
          #endif
      ));
  if (!window_) {
    LOG(ERROR) << "Failed to initialize SDL window";
    return false;
  }
  
  // Set up our custom render pass descriptor
  renderPassDesc.rasterSampleCount = 1;
  renderPassDesc.colorFormats = {MTL::PixelFormatBGRA8Unorm};  // TODO: Use SRGB format PixelFormatBGRA8Unorm_sRGB (once the Shape2D and Text2D/FontStash shaders also support it)
  renderPassDesc.depthFormat = MTL::PixelFormatDepth32Float;
  renderPassDesc.stencilFormat = MTL::PixelFormatInvalid;
  
  // Create Metal view.
  // On macOS, this does *not* associate a MTLDevice with the CAMetalLayer on
  // its own. It is up to user code to do that.
  sdlView = SDL_Metal_CreateView(window_);
  if (!sdlView) {
    LOG(ERROR) << "SDL_Metal_CreateView() failed";
    return false;
  }
  
  // Create Metal device and assign it to the layer
  device = MTL::CreateSystemDefaultDevice();
  LOG(1) << "Using Metal device: " << device->name()->utf8String();
  
  swapChain = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(sdlView));
  swapChain->setDevice(device);
  swapChain->setPixelFormat(renderPassDesc.colorFormats.front());
  
  CA::MetalLayerHelper::SetDisplaySyncEnabled(swapChain, true);
  
  maxFramesInFlight = CA::MetalLayerHelper::GetMaximumDrawableCount(swapChain);
  inFlightSemaphore = dispatch_semaphore_create(maxFramesInFlight);
  
  // Set up the render pass descriptor (aside from the concrete render targets and clear color)
  renderPass = NS::RetainPtr(MTL::RenderPassDescriptor::renderPassDescriptor());
  
  auto colorAttachment = renderPass->colorAttachments()->object(0);
  colorAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
  colorAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
  
  auto depthAttachment = renderPass->depthAttachment();
  depthAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
  depthAttachment->setStoreAction(MTL::StoreAction::StoreActionDontCare);
  depthAttachment->setClearDepth(1.0);
  
  // Create the command queue
  commandQueue = device->newCommandQueue();
  
  // Let the callback object initialize as well.
  if (!callbacks_->Initialize()) {
    return false;
  }
  
  // Create surface dependent objects.
  if (!CreateSurfaceDependentObjects()) {
    return false;
  }
  
  // Let the callback object know about the initial window size.
  // Note that the SDL function to get the drawable size appears to be Metal-specific.
  SDL_Metal_GetDrawableSize(window_, &window_area_width, &window_area_height);
  Resize(window_area_width, window_area_height);
  
  initializationAutoreleasePool.reset();
  appAutoreleasePool = NS::AutoreleasePool::alloc()->init();
  return true;
}

void RenderWindowSDLMetal::GetDrawableSize(int* width, int* height) {
  SDL_Metal_GetDrawableSize(window_, width, height);
}

void RenderWindowSDLMetal::Resize(int width, int height) {
  window_area_width = width;
  window_area_height = height;
  
  auto depthTextureDesc = MTL::TextureDescriptor::texture2DDescriptor(renderPassDesc.depthFormat, width, height, /*mipmapped*/ false);
  depthTextureDesc->setStorageMode(MTL::StorageModePrivate);
  depthTextureDesc->setUsage(MTL::TextureUsageRenderTarget);
  
  depthTexture = NS::TransferPtr(device->newTexture(depthTextureDesc));
  if (!depthTexture) { LOG(ERROR) << "Failed to initialize depthTexture"; return; }
  depthTexture->setLabel(NS::String::string("depthTexture", NS::UTF8StringEncoding));
  
  renderPass->depthAttachment()->setTexture(depthTexture.get());
  
  callbacks_->Resize(width, height);
}

void RenderWindowSDLMetal::Render() {
  NS::AutoreleasePool* perFrameAutoreleasePool = NS::AutoreleasePool::alloc()->init();
  
  currentDrawable = swapChain->nextDrawable();
  renderPass->colorAttachments()->object(0)->setTexture(currentDrawable->texture());
  
  SDL_Metal_GetDrawableSize(window_, &window_area_width, &window_area_height);
  
  currentCmdBuf = commandQueue->commandBuffer();
  
  dispatch_semaphore_wait(inFlightSemaphore, DISPATCH_TIME_FOREVER);
  
  callbacks_->Render(/*imageIndex: invalid*/ 0);
  
  currentCmdBuf->presentDrawable(currentDrawable);
  
  {
    lock_guard<mutex> lock(currentFramesInFlightMutex);
    ++ currentFramesInFlight;
  }
  currentCmdBuf->addCompletedHandler([this](MTL::CommandBuffer* /*cmdBuf*/) {
    dispatch_semaphore_signal(inFlightSemaphore);
    
    {
      lock_guard<mutex> lock(currentFramesInFlightMutex);
      -- currentFramesInFlight;
    }
    currentFramesInFlightDecreasedCondition.notify_all();
  });
  currentCmdBuf->commit();
  
  perFrameAutoreleasePool->release();
}

bool RenderWindowSDLMetal::CreateSurfaceDependentObjects() {
  return callbacks_->InitializeSurfaceDependent();
}

void RenderWindowSDLMetal::DestroySurfaceDependentObjects() {}

}

#endif
