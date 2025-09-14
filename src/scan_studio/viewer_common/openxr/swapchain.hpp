#pragma once
#ifdef HAVE_OPENXR

#include <libvis/vulkan/framebuffer.h>
#include <libvis/vulkan/image.h>
#include <libvis/vulkan/image_view.h>
#include <libvis/vulkan/libvis.h>
#include <libvis/vulkan/render_pass.h>

#include "scan_studio/viewer_common/openxr/openxr.hpp"
#include "scan_studio/viewer_common/openxr/loader.hpp"

namespace vis {
  class VulkanFramebuffer;
}

namespace scan_studio {
using namespace vis;

class OpenXRSwapchain {
 public:
  /// This does not initialize the object yet. Initialize() must be called for that.
  OpenXRSwapchain();
  
  OpenXRSwapchain(const OpenXRSwapchain& other) = delete;
  OpenXRSwapchain& operator= (const OpenXRSwapchain& other) = delete;
  
  OpenXRSwapchain(OpenXRSwapchain&& other);
  OpenXRSwapchain& operator= (OpenXRSwapchain&& other);
  
  /// Destroys the swapchain.
  ~OpenXRSwapchain();
  
  /// Attempts to initialize the OpenXR swapchain.
  /// Returns true if successful, false otherwise.
  bool Initialize(
      VkFormat format,
      VkSampleCountFlagBits msaaSamples,
      VkFormat depthFormat,
      bool singleDepthImageOnly,
      const XrViewConfigurationView& view,
      XrSession session,
      VkRenderPass renderPass,
      const VulkanDevice& device);
  void Destroy();
  
  /// Returns the framebuffer for the given swapchain image indices.
  inline const VulkanFramebuffer& GetSwapChainFramebuffer(u32 colorImageIndex, u32 depthImageIndex) const {
    return swapchainFramebuffers[colorImageIndex * depthSwapchainImageViews.size() + depthImageIndex];
  }
  inline VulkanFramebuffer& GetSwapChainFramebuffer(u32 colorImageIndex, u32 depthImageIndex) {
    return swapchainFramebuffers[colorImageIndex * depthSwapchainImageViews.size() + depthImageIndex];
  }
  
  inline u32 GetWidth() const { return width; }
  inline u32 GetHeight() const { return height; }
  inline u32 GetImageCount() const { return swapchainImages.size(); }
  
  /// Returns the underlying XrSwapchain handle.
  inline const XrSwapchain& GetColorSwapchain() const { return swapchain; }
  inline const XrSwapchain& GetDepthSwapchain() const { return depthSwapchain; }
  
 private:
  XrSwapchain swapchain = XR_NULL_HANDLE;
  VulkanImage msaaImage;
  VulkanImageView msaaImageView;
  
  XrSwapchain depthSwapchain = XR_NULL_HANDLE;
  VulkanImage singleDepthImage;
  
  u32 width;
  u32 height;
  vector<XrSwapchainImageVulkanKHR> swapchainImages;
  vector<XrSwapchainImageVulkanKHR> depthSwapchainImages;
  
  /// Contains one image view for each swap chain image
  vector<VulkanImageView> swapchainImageViews;
  vector<VulkanImageView> depthSwapchainImageViews;
  
  /// Contains one framebuffer for each swap chain image
  vector<VulkanFramebuffer> swapchainFramebuffers;
};

}

#endif
