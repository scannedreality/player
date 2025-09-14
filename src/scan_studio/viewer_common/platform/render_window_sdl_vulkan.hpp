#pragma once
#ifdef HAVE_VULKAN

#include <vector>

#include <libvis/util/window_callbacks.h>
#include <libvis/vulkan/forward_definitions.h>
#include <libvis/vulkan/image.h>
#include <libvis/vulkan/image_view.h>
#include <libvis/vulkan/instance.h>
#include <libvis/vulkan/render_pass.h>

#include "scan_studio/viewer_common/platform/render_window_sdl.hpp"

struct SDL_Window;

namespace vis {

class RenderWindowSDLVulkan : public scan_studio::RenderWindowSDL {
 public:
  RenderWindowSDLVulkan();
  ~RenderWindowSDLVulkan();
  
  /// --- Setters which may be used to configure the render window before calling Initialize(). ---
  /// --- If the setters are not used, default values will be used for the settings.            ---
  
  /// Sets the requested Vulkan version. Use a macro such as for example VK_API_VERSION_1_0 or VK_API_VERSION_1_2 for the parameter.
  /// Default: VK_API_VERSION_1_0.
  void SetRequestedVulkanVersion(u32 version);
  
  /// Sets whether to enable debug layers.
  /// Default: false.
  void SetEnableDebugLayers(bool enable);
  
  /// TODO: Document what this does exactly.
  /// Default: true.
  void SetUseTransientDefaultCommandBuffers(bool enable);
  
  virtual void Deinitialize() override;
  
  /// Retrieves the command buffer vector. There is one command buffer for each frame in flight.
  inline vector<VulkanCommandBuffer>* GetCommandBuffers() { return &command_buffers_; }
  inline VulkanCommandBuffer& GetCurrentCommandBuffer() { return command_buffers_[current_frame_in_flight_]; }
  
  /// Retrieves the swap chain framebuffer vector, with one item for each swap chain image.
  inline vector<VulkanFramebuffer>* GetSwapChainFramebuffers() { return &swap_chain_framebuffers_; }
  
  inline VulkanImage& GetDepthStencilImage() { return depth_stencil_image_; }
  
  inline const VkExtent2D& GetSwapChainExtent() const { return swap_chain_extent_; }
  
  /// Returns a "default" render pass which renders to the color buffer (only).
  /// TODO: Is this useful in practice? Or better remove it?
  VulkanRenderPass* GetDefaultRenderPass() { return &render_pass_; }
  
  inline bool IsSRGBRenderTargetUsed() const { return is_srgb_render_target_used_; }
  
  inline VulkanInstance& GetInstance() { return instance_; }
  inline VulkanDevice& GetDevice() { return device_; }
  inline VulkanCommandPool& GetCommandPool() { return command_pool_; }
  
  inline int GetSwapChainImageCount() const { return swap_chain_images_.size(); }
  inline int GetMaxFramesInFlight() const { return max_frames_in_flight_; }
  inline int GetCurrentFrameInFlight() const { return current_frame_in_flight_; }
  
 protected:
  virtual bool InitializeImpl(const char *title, int width, int height, WindowState windowState) override;
  virtual void GetDrawableSize(int *width, int *height) override;
  virtual void Resize(int width, int height) override;
  virtual void Render() override;
  
 private:
  void RecreateSwapChain();
  
  void StopAndWaitForRendering();
  bool CreateSurfaceDependentObjects(VkSwapchainKHR old_swap_chain);
  void DestroySurfaceDependentObjects();
  
  VulkanInstance instance_;
  VulkanPhysicalDevice* selected_physical_device_ = nullptr;
  VulkanQueue* graphics_queue_ = nullptr;
  VulkanQueue* presentation_queue_ = nullptr;
  VulkanDevice device_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swap_chain_ = VK_NULL_HANDLE;
  VulkanImage msaa_image_;
  VulkanImageView msaa_image_view_;
  VkSampleCountFlagBits msaa_samples_;
  bool is_srgb_render_target_used_;
  VkExtent2D swap_chain_extent_;
  vector<VkImage> swap_chain_images_;  // vector has swapChainImageCount items
  vector<VulkanImageView> swap_chain_image_views_;  // vector has swapChainImageCount items
  VulkanImage depth_stencil_image_;
  VulkanImageView depth_stencil_image_view_;
  VulkanRenderPass render_pass_;
  vector<VulkanFramebuffer> swap_chain_framebuffers_;  // vector has swapChainImageCount items
  VulkanCommandPool command_pool_;
  // TODO: Using one command buffer per swap chain image was based on the Vulkan tutorial's assumption
  //       that command buffers are recorded for each swap chain image and then not modified anymore.
  //       In practice, these are however re-recorded for each frame in flight. Should this be adapted to that,
  //       such that we allocate one command buffer per frame-in-flight instead?
  vector<VulkanCommandBuffer> command_buffers_;  // vector has max_frames_in_flight_ items
  vector<VkSemaphore> image_available_semaphores_;  // vector has max_frames_in_flight_ items
  vector<VkSemaphore> render_finished_semaphores_;  // vector has max_frames_in_flight_ items
  vector<VulkanFence> in_flight_fences_;  // vector has max_frames_in_flight_ items
  
  /// The current index into vectors having an item for each frame in flight; in [0, max_frames_in_flight_[
  int current_frame_in_flight_;
  
  /// The number of frames that we submit on the CPU side without waiting for the GPU
  /// to finish rendering of the last frame before. With constant CPU and GPU times per frame,
  /// two would be enough to avoid unnecessary CPU or GPU stalls. However, three are recommended
  /// since they might be able to smooth the frame rate in case of differing frame times. See:
  /// https://software.intel.com/content/www/us/en/develop/articles/practical-approach-to-vulkan-part-1.html
  /// However, consider that rendering three frames in advance causes significant input latency.
  /// I found two to result in strongly reduced perceived input latency.
  int max_frames_in_flight_;
  
  // Pre-init configuration
  u32 requested_vulkan_version_ = VK_API_VERSION_1_0;
  bool enable_debug_layers_ = false;
  bool transient_default_command_buffers_ = true;
};

}

#endif
