#pragma once

#include <loguru.hpp>

#include <libvis/vulkan/libvis.h>

namespace vis {
  struct MetalRenderPassDescriptor;
  class VulkanCommandBuffer;
  class VulkanDevice;
  class VulkanRenderPass;
}

namespace MTL {
  class CommandBuffer;
  class Device;
  class RenderCommandEncoder;
}

struct ID3D11Device5;

namespace scan_studio {
using namespace vis;

struct VulkanRenderState;
class MetalLibraryCache;
struct MetalRenderState;
struct OpenGLRenderState;
struct D3D11RenderState;
#ifdef HAVE_OPENXR
class OpenXRVulkanApplication;
#endif

/// Base class for render states.
///
/// Render states are used to pass rendering API specific parameters to render functions,
/// while using a common signature for those functions.
///
/// TODO: It looks like it would make sense to differentiate between the 'general' attributes (device, frame-in-flight-count, ...)
///       and those that are for a specific rendering iteration (cmdBuf, command encoder, ...). Some functions only need the former,
///       and currently we mash all of it into a single struct that may then be partially uninitialized in case the latter is not available.
///       Possible names:
///       - RenderContext for the general attributes only
///       - RenderCommandContext for everything including the render command buffer / encoder attributes
struct RenderState {
  enum class RenderingAPI {
    OpenGL = 0,
    Vulkan,
    Metal,
    D3D11
  };
  
  inline RenderState(RenderingAPI api)
      : api(api) {}
  
  inline virtual ~RenderState() {}
  
  inline VulkanRenderState* AsVulkanRenderState() {
    CHECK(api == RenderingAPI::Vulkan);
    return reinterpret_cast<VulkanRenderState*>(this);
  }
  
  inline MetalRenderState* AsMetalRenderState() {
    CHECK(api == RenderingAPI::Metal);
    return reinterpret_cast<MetalRenderState*>(this);
  }
  
  inline OpenGLRenderState* AsOpenGLRenderState() {
    CHECK(api == RenderingAPI::OpenGL);
    return reinterpret_cast<OpenGLRenderState*>(this);
  }
  
  inline D3D11RenderState* AsD3D11RenderState() {
    CHECK(api == RenderingAPI::D3D11);
    return reinterpret_cast<D3D11RenderState*>(this);
  }
  
  RenderingAPI api;
};

struct VulkanRenderState : public RenderState {
  inline VulkanRenderState()
      : RenderState(RenderState::RenderingAPI::Vulkan) {}
  
  inline VulkanRenderState(
      VulkanDevice* device,
      const VulkanRenderPass* renderPass,
      int framesInFlightCount,
      VulkanCommandBuffer* cmdBuf,
      int frameInFlightIndex)
      : RenderState(RenderState::RenderingAPI::Vulkan),
        device(device),
        renderPass(renderPass),
        framesInFlightCount(framesInFlightCount),
        cmdBuf(cmdBuf),
        frameInFlightIndex(frameInFlightIndex) {}
  
  #ifdef HAVE_OPENXR
  explicit VulkanRenderState(VulkanCommandBuffer* cmdBuf, OpenXRVulkanApplication* app);
  #endif
  
  VulkanRenderState(VulkanRenderState&& other) = default;
  VulkanRenderState& operator= (VulkanRenderState&& other) = default;
  
  VulkanRenderState(const VulkanRenderState& other) = delete;
  VulkanRenderState& operator= (const VulkanRenderState& other) = delete;
  
  // General:
  VulkanDevice* device;
  const VulkanRenderPass* renderPass;
  int framesInFlightCount;
  
  // For rendering a specific frame:
  VulkanCommandBuffer* cmdBuf;
  int frameInFlightIndex;
};

struct MetalRenderState : public RenderState {
  inline MetalRenderState()
      : RenderState(RenderState::RenderingAPI::Metal) {}
  
  // General:
  inline MetalRenderState(
      MTL::Device* device,
      MetalLibraryCache* libraryCache,
      MetalRenderPassDescriptor* renderPassDesc,
      int framesInFlightCount)
      : RenderState(RenderState::RenderingAPI::Metal),
        device(device),
        libraryCache(libraryCache),
        renderPassDesc(renderPassDesc),
        framesInFlightCount(framesInFlightCount),
        cmdBuf(nullptr),
        renderCmdEncoder(nullptr) {}
  
  // For rendering a specific frame:
  inline MetalRenderState(
      MTL::Device* device,
      MetalLibraryCache* libraryCache,
      MetalRenderPassDescriptor* renderPassDesc,
      int framesInFlightCount,
      MTL::CommandBuffer* cmdBuf)
      : RenderState(RenderState::RenderingAPI::Metal),
        device(device),
        libraryCache(libraryCache),
        renderPassDesc(renderPassDesc),
        framesInFlightCount(framesInFlightCount),
        cmdBuf(cmdBuf),
        renderCmdEncoder(nullptr) {}
  
  // General:
  MTL::Device* device;
  MetalLibraryCache* libraryCache;
  MetalRenderPassDescriptor* renderPassDesc;
  int framesInFlightCount;
  
  // For rendering a specific frame:
  MTL::CommandBuffer* cmdBuf;
  MTL::RenderCommandEncoder* renderCmdEncoder;
};

struct OpenGLRenderState : public RenderState {
  inline OpenGLRenderState()
      : RenderState(RenderState::RenderingAPI::OpenGL) {}
};

struct D3D11RenderState : public RenderState {
  inline D3D11RenderState()
      : RenderState(RenderState::RenderingAPI::D3D11) {}
  
  inline D3D11RenderState(ID3D11Device5 * device)
      : RenderState(RenderState::RenderingAPI::D3D11),
        device(device) {}
  
  D3D11RenderState(D3D11RenderState&& other) = default;
  D3D11RenderState& operator= (D3D11RenderState&& other) = default;
  
  D3D11RenderState(const D3D11RenderState& other) = delete;
  D3D11RenderState& operator= (const D3D11RenderState& other) = delete;
  
  // General:
  ID3D11Device5* device = nullptr;
};

}
