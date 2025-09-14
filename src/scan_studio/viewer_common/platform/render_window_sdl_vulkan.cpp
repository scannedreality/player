#ifdef HAVE_VULKAN
#include "scan_studio/viewer_common/platform/render_window_sdl_vulkan.hpp"

#include <algorithm>
#include <array>

#include <SDL_vulkan.h>

#include <loguru.hpp>

#include <libvis/vulkan/fence.h>
#include <libvis/vulkan/framebuffer.h>
#include <libvis/vulkan/physical_device.h>
#include <libvis/vulkan/queue.h>

namespace vis {

RenderWindowSDLVulkan::RenderWindowSDLVulkan()
    : current_frame_in_flight_(0),
      max_frames_in_flight_(2) {}  // TODO: Make max_frames_in_flight_ dependent on the swap chain image count?

RenderWindowSDLVulkan::~RenderWindowSDLVulkan() {
  Deinitialize();
}

void RenderWindowSDLVulkan::SetRequestedVulkanVersion(u32 version) {
  requested_vulkan_version_ = version;
}

void RenderWindowSDLVulkan::SetEnableDebugLayers(bool enable) {
  enable_debug_layers_ = enable;
}

void RenderWindowSDLVulkan::SetUseTransientDefaultCommandBuffers(bool enable) {
  transient_default_command_buffers_ = enable;
}

void RenderWindowSDLVulkan::Deinitialize() {
  StopAndWaitForRendering();
  
  if (callbacks_) { callbacks_->DeinitializeSurfaceDependent(); }
  DestroySurfaceDependentObjects();
  if (callbacks_) {
    callbacks_->Deinitialize();
    callbacks_.reset();
  }
  
  command_buffers_.clear();
  
  const auto& api = device_.Api();
  
  if (swap_chain_ != VK_NULL_HANDLE) {
    api.vkDestroySwapchainKHR(device_, swap_chain_, nullptr);
    swap_chain_ = VK_NULL_HANDLE;
  }
  
  for (usize i = 0; i < image_available_semaphores_.size(); ++ i) {
    api.vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
    api.vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
  }
  image_available_semaphores_.clear();
  render_finished_semaphores_.clear();
  
  if (surface_ != VK_NULL_HANDLE) {
    instance_.Api().vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
  
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

bool RenderWindowSDLVulkan::InitializeImpl(const char* title, int width, int height, WindowState windowState) {
  #ifdef __MAC_OS_X_VERSION_MAX_ALLOWED
    // On macOS, try to make SDL use the Vulkan loader provided by the Vulkan SDK,
    // as this allows us to use the validation layers.
    // (On iOS, it is required to link statically to MoltenVK,
    //  which does not allow using the validation layers.)
    // The list of library names that we try here corresponds to what volkInitialize() tries.
    // Note: Probably, this is only necessary since we currently *also* statically link to MoltenVK on macOS.
    //       However, that might be okay this way, since that provides us with a fallback in case the MoltenVK library is
    //       not available system-wide.
    if (SDL_Vulkan_LoadLibrary("libvulkan.dylib") != 0 &&
        SDL_Vulkan_LoadLibrary("libvulkan.1.dylib") != 0 &&
        SDL_Vulkan_LoadLibrary("libMoltenVK.dylib") != 0) {
      LOG(WARNING) << "Failed to load any of libvulkan.1.dylib, libvulkan.dylib, or libMoltenVK.dylib. As a result, the validation layers might not be available.";
    }
  #endif
  
  // Setup window
  window_ = SDL_CreateWindow(
      title,
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height,
      static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
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
    LOG(ERROR) << "Failed to initialize SDL window; SDL_GetError(): " << SDL_GetError();
    Deinitialize(); return false;
  }
  
  // Get required extensions
  uint32_t extensions_count = 0;
  SDL_Vulkan_GetInstanceExtensions(window_, &extensions_count, nullptr);
  const char** extensions = new const char*[extensions_count];
  SDL_Vulkan_GetInstanceExtensions(window_, &extensions_count, extensions);
  vector<string> instanceExtensions(extensions_count);
  for (usize i = 0; i < extensions_count; ++ i) {
    instanceExtensions[i] = extensions[i];
  }
  delete[] extensions;
  
  // TODO: Should enabling the additional debug layers be configurable separately from the "basic" debug layers?
  const bool enable_additional_debug_layers = enable_debug_layers_;
  if (enable_additional_debug_layers) {
    LOG(WARNING) << "Enabling additional Vulkan debug layers!";
    instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
  }
  
  vector<string> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  callbacks_->SpecifyAdditionalExtensions(&instanceExtensions, &deviceExtensions);
  
  // Enable additional debug layers
  array<VkValidationFeatureEnableEXT, 3> validationFeatureEnable = {
      VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
      // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
      // VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,  // Cannot be used at the same time as VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
  
  VkValidationFeaturesEXT validationFeatures{};
  if (enable_additional_debug_layers) {
    validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validationFeatures.enabledValidationFeatureCount = validationFeatureEnable.size();
    validationFeatures.pEnabledValidationFeatures = validationFeatureEnable.data();
  }
  
  // Initialize the Vulkan instance.
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  // TODO: For some obscure reason, using the vkGetInstanceProc pointer from SDL, as used below on macOS and iOS,
  //       causes vkDestroyDevice() to crash on Linux (but everything else up to vkDestroyDevice() works).
  #if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) || defined(TARGET_OS_IOS)
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
  #endif
  if (!instance_.Initialize(requested_vulkan_version_, instanceExtensions, enable_debug_layers_, enable_additional_debug_layers ? &validationFeatures : nullptr, vkGetInstanceProcAddr)) {
    LOG(ERROR) << "Failed to initialize Vulkan instance";
    Deinitialize(); return false;
  }
  
  // Create Window Surface
  if (SDL_Vulkan_CreateSurface(window_, instance_, &surface_) == SDL_FALSE) {
    LOG(ERROR) << "Failed to create Vulkan surface";
    Deinitialize(); return false;
  }
  
  // Find the best suited physical device.
  u32 selected_graphics_queue_family_index;
  u32 selected_presentation_queue_family_index;
  u32 selected_transfer_queue_family_index;
  instance_.FindBestPhysicalDeviceForPresentation(
      &surface_,
      deviceExtensions,
      &selected_physical_device_,
      &selected_graphics_queue_family_index,
      &selected_presentation_queue_family_index,
      &selected_transfer_queue_family_index);
  if (!selected_physical_device_) {
    LOG(ERROR) << "No suitable Vulkan device found.";
    Deinitialize(); return false;
  }

  LOG(1) << "Selected Vulkan physical device: " << selected_physical_device_->properties().deviceName;
  
  // Create a logical device for the selected physical device.
  const u32 desired_graphics_queue_count = 1;  // TODO: Make configurable; NOTE: On an AMD RX 6600, the maximum possible count here was 1.
  const u32 desired_presentation_queue_count = 1;  // TODO: Make configurable
  const u32 desired_transfer_queue_count = 2;  // TODO: Make configurable
  
  const u32 max_graphics_queue_count = selected_physical_device_->queue_families()[selected_graphics_queue_family_index].queueCount;
  const u32 max_presentation_queue_count = selected_physical_device_->queue_families()[selected_presentation_queue_family_index].queueCount;
  const u32 max_transfer_queue_count = selected_physical_device_->queue_families()[selected_transfer_queue_family_index].queueCount;
  
  LOG(1) << "selected_graphics_queue_family_index: " << selected_graphics_queue_family_index << ", queue count: " << max_graphics_queue_count;
  LOG(1) << "selected_presentation_queue_family_index: " << selected_presentation_queue_family_index << ", queue count: " << max_presentation_queue_count;
  LOG(1) << "selected_transfer_queue_family_index: " << selected_transfer_queue_family_index << ", queue count: " << max_transfer_queue_count;
  
  if (desired_graphics_queue_count > max_graphics_queue_count) {
    LOG(WARNING) << "desired_graphics_queue_count is " << desired_graphics_queue_count << ", but only " << max_graphics_queue_count << " queues are available";
  }
  if (desired_presentation_queue_count > max_presentation_queue_count) {
    LOG(WARNING) << "desired_presentation_queue_count is " << desired_presentation_queue_count << ", but only " << max_presentation_queue_count << " queues are available";
  }
  if (desired_transfer_queue_count > max_transfer_queue_count) {
    LOG(WARNING) << "desired_transfer_queue_count is " << desired_transfer_queue_count << ", but only " << max_transfer_queue_count << " queues are available";
  }
  
  device_.RequestQueues(selected_graphics_queue_family_index, /*count*/ std::min(desired_graphics_queue_count, max_graphics_queue_count));
  device_.SetGraphicsQueueFamilyIndex(selected_graphics_queue_family_index);
  
  device_.RequestQueues(selected_presentation_queue_family_index, /*count*/ std::min(desired_presentation_queue_count, max_presentation_queue_count));
  device_.SetPresentationQueueFamilyIndex(selected_presentation_queue_family_index);
  
  device_.RequestQueues(selected_transfer_queue_family_index, /*count*/ std::min(desired_transfer_queue_count, max_transfer_queue_count));
  device_.SetTransferQueueFamilyIndex(selected_transfer_queue_family_index);
  
  VkSampleCountFlags msaa_sample_count_flags = VK_SAMPLE_COUNT_1_BIT;
  VkPhysicalDeviceFeatures enabled_features{};
  const void* deviceCreateInfoPNext = nullptr;
  callbacks_->PreInitialize(selected_physical_device_, &msaa_sample_count_flags, &enabled_features, &deviceExtensions, &deviceCreateInfoPNext);
  msaa_samples_ = static_cast<VkSampleCountFlagBits>(msaa_sample_count_flags);
  if (!device_.Initialize(*selected_physical_device_, enabled_features, deviceExtensions, instance_, deviceCreateInfoPNext)) {
    Deinitialize(); return false;
  }
  
  graphics_queue_ = device_.GetQueue(selected_graphics_queue_family_index, /*queue_index*/ 0);
  presentation_queue_ = device_.GetQueue(selected_presentation_queue_family_index, /*queue_index*/ 0);
  
  // Create command pool.
  // Possible flags:
  // - VK_COMMAND_POOL_CREATE_TRANSIENT_BIT:
  //     Hint that command buffers are re-recorded very often.
  // - VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT:
  //     Allows command buffers to be re-recorded individually.
  if (!command_pool_.Initialize(selected_graphics_queue_family_index, (transient_default_command_buffers_ ? VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 0) | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, device_)) {
    LOG(ERROR) << "Failed to create a command pool.";
    Deinitialize(); return false;
  }
  
  // Create command buffers: one for each frame in flight.
  command_buffers_.resize(max_frames_in_flight_);
  for (usize i = 0; i < command_buffers_.size(); ++ i) {
    if (!command_buffers_[i].Initialize(VK_COMMAND_BUFFER_LEVEL_PRIMARY, command_pool_)) {
      LOG(ERROR) << "Failed to allocate command buffers.";
      Deinitialize(); return false;
    }
    ostringstream iStr;
    iStr << i;
    command_buffers_[i].SetDebugNameIfDebugging(("RenderWindowSDLVulkan command_buffers_[" + iStr.str() + "]").c_str());
  }
  
  // Create semaphores for swap chain synchronization (retrieval, rendering, presenting)
  // and create in-flight fences (for waiting for frames to finish rendering on the GPU).
  VkSemaphoreCreateInfo semaphore_info = {};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  
  image_available_semaphores_.resize(max_frames_in_flight_);
  render_finished_semaphores_.resize(max_frames_in_flight_);
  in_flight_fences_.resize(max_frames_in_flight_);
  
  const auto& api = device_.Api();
  for (int i = 0; i < max_frames_in_flight_; ++ i) {
    if (api.vkCreateSemaphore(device_.device(), &semaphore_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
        api.vkCreateSemaphore(device_.device(), &semaphore_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
        !in_flight_fences_[i].Initialize(/*create_signaled*/ true, device_)) {
      LOG(ERROR) << "Failed to create a semaphore or fence.";
      Deinitialize(); return false;
    }
  }
  
  // Let the callback object initialize as well.
  if (!callbacks_->Initialize()) {
    Deinitialize(); return false;
  }
  
  // Create surface dependent objects.
  if (!CreateSurfaceDependentObjects(VK_NULL_HANDLE)) {
    DestroySurfaceDependentObjects(); Deinitialize(); return false;
  }
  
  // Let the callback object know about the initial window size.
  // Note that the SDL function to get the drawable size appears to be Vulkan-specific.
  SDL_Vulkan_GetDrawableSize(window_, &window_area_width, &window_area_height);
  callbacks_->Resize(window_area_width, window_area_height);
  
  return true;
}

void RenderWindowSDLVulkan::GetDrawableSize(int* width, int* height) {
  SDL_Vulkan_GetDrawableSize(window_, width, height);
}

void RenderWindowSDLVulkan::Resize(int /*width*/, int /*height*/) {
  RecreateSwapChain();
}

void RenderWindowSDLVulkan::Render() {
  const auto& api = device_.Api();
  
  // Ensure that we can use the resources for this frame-in-flight by waiting for the potentially previously
  // submitted frame with these frame-in-flight resources to finish rendering.
  in_flight_fences_[current_frame_in_flight_].Wait();
  
  // Asynchronously get the next image from the swap chain.
  // The (immediately) returned image_index refers to the swap_chain_images_ array.
  // We have to wait for image_available_semaphore_ to become signaled before using the image.
  // The semaphore being signaled means that the presentation engine finished reading from the image.
  // I believe that this also means that we finished writing to the image,
  // otherwise the presentation engine could not have started reading from it.
  u32 image_index;
  VkResult result = api.vkAcquireNextImageKHR(
      device_.device(),
      swap_chain_,
      /* timeout */ numeric_limits<uint64_t>::max(),
      image_available_semaphores_[current_frame_in_flight_],
      VK_NULL_HANDLE,
      &image_index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    RecreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    LOG(ERROR) << "Failed to acquire swap chain image.";
    return;
  }
  
  // Let the callbacks update render states (for example, uniform buffers)
  callbacks_->Render(image_index);
  
  // Submit the right command buffer for the image.
  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  // Wait with the color attachment output until the image_available_semaphore_
  // was signaled.
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_available_semaphores_[current_frame_in_flight_];
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.pWaitDstStageMask = wait_stages;
  // Specify the command buffer(s) to submit.
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffers_[current_frame_in_flight_].buffer();
  // Signal the render_finished_semaphore_ after the command buffers finished.
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &render_finished_semaphores_[current_frame_in_flight_];
  
  in_flight_fences_[current_frame_in_flight_].Reset();
  graphics_queue_->Lock();
  if (api.vkQueueSubmit(*graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_in_flight_]) != VK_SUCCESS) {
    LOG(ERROR) << "Failed to submit draw command buffer.";
  }
  graphics_queue_->Unlock();
  
  // Put the image back into the swap chain for presentation.
  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished_semaphores_[current_frame_in_flight_];
  
  VkSwapchainKHR swap_chains[] = {swap_chain_};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swap_chains;
  present_info.pImageIndices = &image_index;
  // Only useful if using more than one swap chain.
  present_info.pResults = nullptr;
  
  presentation_queue_->Lock();
  result = api.vkQueuePresentKHR(*presentation_queue_, &present_info);
  presentation_queue_->Unlock();
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    RecreateSwapChain();
  } else if (result != VK_SUCCESS) {
    LOG(ERROR) << "Failed to present swap chain image.";
    return;
  }
  
  current_frame_in_flight_ = (current_frame_in_flight_ + 1) % max_frames_in_flight_;
}

void RenderWindowSDLVulkan::RecreateSwapChain() {
  // TODO: Resizing the render window probably stutters because of this StopAndWaitForRendering(). Can we avoid this?
  StopAndWaitForRendering();
  
  // TODO: It seems like a more elaborate solution to find the objects to
  //       recreate could pay off here. For example, some dependent objects
  //       may only need to be recreated if the swap chain's format changes,
  //       which is extremely unlikely.
  callbacks_->DeinitializeSurfaceDependent();
  DestroySurfaceDependentObjects();
  if (!CreateSurfaceDependentObjects(swap_chain_)) {
    LOG(ERROR) << "CreateSurfaceDependentObjects() failed while recreating swap chain";
  }
}

void RenderWindowSDLVulkan::StopAndWaitForRendering() {
  // Note: vkDeviceWaitIdle() requires synchronizing host access to all queues.
  //       This means that it might be an issue if for example a background thread is still
  //       submitting to the transfer queue while rendering is shutting down. Thus, we only
  //       wait for the specific rendering queues to be idle here.
  if (graphics_queue_ && !graphics_queue_->WaitIdle()) { LOG(ERROR) << "Failure in vkQueueWaitIdle(*graphics_queue_)"; }
  if (presentation_queue_ && !presentation_queue_->WaitIdle()) { LOG(ERROR) << "Failure in vkQueueWaitIdle(*presentation_queue_)"; }
}

bool RenderWindowSDLVulkan::CreateSurfaceDependentObjects(VkSwapchainKHR old_swap_chain) {
  int remainingAttempts = 10;
retry:;
  const auto& api = device_.Api();
  
  auto getPresentModeName = [](VkPresentModeKHR mode) {
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      return "immediate";
    } else if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return "mailbox";
    } else if (mode == VK_PRESENT_MODE_FIFO_KHR) {
      return "fifo";
    } else if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
      return "fifo_relaxed";
    } else if (mode == VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR) {
      return "shared_demand_refresh";
    } else if (mode == VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR) {
      return "shared_continuous_refresh";
    } else {
      return "(unknown mode)";
    }
  };
  
  // Gather information for creating a swap chain.
  VulkanSwapChainSupport swap_chain_support;
  selected_physical_device_->QuerySwapChainSupport(surface_, &swap_chain_support);
  if (VLOG_IS_ON(1)) {
    VLOG(1) << "Selected swap chain:";
    VLOG(1) << "  minImageCount: " << swap_chain_support.capabilities.minImageCount;
    VLOG(1) << "  maxImageCount: " << swap_chain_support.capabilities.maxImageCount;
    VLOG(1) << "  currentExtent: (" << swap_chain_support.capabilities.currentExtent.width << ", " << swap_chain_support.capabilities.currentExtent.height << ")";
    VLOG(1) << "  minImageExtent: (" << swap_chain_support.capabilities.minImageExtent.width << ", " << swap_chain_support.capabilities.minImageExtent.height << ")";
    VLOG(1) << "  maxImageExtent: (" << swap_chain_support.capabilities.maxImageExtent.width << ", " << swap_chain_support.capabilities.maxImageExtent.height << ")";
    VLOG(1) << "  maxImageArrayLayers: " << swap_chain_support.capabilities.maxImageArrayLayers;
    VLOG(1) << "  supportedTransforms: " << swap_chain_support.capabilities.supportedTransforms;
    VLOG(1) << "  currentTransform: " << swap_chain_support.capabilities.currentTransform;
    VLOG(1) << "  supportedCompositeAlpha: " << swap_chain_support.capabilities.supportedCompositeAlpha;
    VLOG(1) << "  supportedUsageFlags: " << swap_chain_support.capabilities.supportedUsageFlags;
    VLOG(1) << "  #supported formats: " << swap_chain_support.formats.size();
    string present_mode_names;
    for (VkPresentModeKHR mode : swap_chain_support.present_modes) {
      if (!present_mode_names.empty()) {
        present_mode_names += ", ";
      }
      present_mode_names += getPresentModeName(mode);
    }
    VLOG(1) << "  supported present modes: " << present_mode_names;
  }
  
  // Find the best available surface format. This determines the way colors are
  // represented. We prefer a standard 32 bits-per-pixel sRGB
  // format in BGRA pixel ordering (the corresponding RGBA format wasn't
  // available on an Intel IvyBridge mobile GPU), and sRGB color space.
  // NOTE: The only other available format on the IvyBridge mobile GPU is:
  // VK_FORMAT_R8G8B8A8_UNORM.
  VkSurfaceFormatKHR preferred_surface_format;
  // TODO: Go through a list of preferred formats instead of only one: VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
  preferred_surface_format.format = VK_FORMAT_B8G8R8A8_SRGB;  // means that values written to the surface will be implicitly converted from linear to sRGB color space during writing.
  preferred_surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;  // means that values in the surface will be interpreted as being in sRGB color space when the driver displays the surface.
  VkSurfaceFormatKHR selected_surface_format{};
  if (swap_chain_support.formats.size() == 1 &&
      swap_chain_support.formats[0].format == VK_FORMAT_UNDEFINED) {
    // Any format is allowed. Choose our preferred one.
    selected_surface_format = preferred_surface_format;
  } else {
    // Go through the list of supported formats to see if the preferred format
    // is supported.
    bool found = false;
    for (const auto& available_format : swap_chain_support.formats) {
      if (available_format.format == preferred_surface_format.format &&
          available_format.colorSpace == preferred_surface_format.colorSpace) {
        selected_surface_format = preferred_surface_format;
        found = true;
        break;
      }
    }
    if (!found) {
      // The preferred surface format is not available. Simply choose the first
      // available one.
      // TODO: Could rank the available formats and choose the best.
      LOG(WARNING) << "The preferred surface format for the swap chain is not available. Choosing format: " << swap_chain_support.formats[0].format;
      LOG(WARNING) << "Available formats:";
      for (const auto& available_format : swap_chain_support.formats) {
        LOG(WARNING) << "  VkFormat " << available_format.format << " VkColorSpaceKHR " << available_format.colorSpace;
      }
      selected_surface_format = swap_chain_support.formats[0];
    }
  }
  
  is_srgb_render_target_used_ = VulkanIsSRGBFormat(selected_surface_format.format);
  
  // Find the best available presentation mode.
  // Out of the 4 modes offered by Vulkan, 2 are interesing for us (since they
  // are the ones to avoid tearing):
  // VK_PRESENT_MODE_FIFO_KHR: The swap chain is a queue. On a vertical blank,
  //     the first image is taken and displayed. If the queue is full, the
  //     application has to wait. Similar to VSync. This mode is guaranteed to
  //     be supported.
  // VK_PRESENT_MODE_MAILBOX_KHR: Differing from the mode above, if the queue is
  //     full, new images can replace the existing ones. Can be used to
  //     implement triple buffering.
  VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
  bool present_mode_found = false;
  if (swap_chain_support.present_modes.empty()) {
    LOG(ERROR) << "No present mode available.";
    return false;
  }
  for (VkPresentModeKHR present_mode : swap_chain_support.present_modes) {
    if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
      selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
      present_mode_found = true;
      break;  // always use this mode if it is available
    } else if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      selected_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
      present_mode_found = true;
    }
  }
  if (!present_mode_found) {
    selected_present_mode = swap_chain_support.present_modes.front();
  }
  LOG(1) << "Selected present mode: " << getPresentModeName(selected_present_mode);
  
  // Choose the resolution of the swap chain images equal to the window size,
  // if possible.
  if (swap_chain_support.capabilities.currentExtent.width != numeric_limits<uint32_t>::max()) {
    // The extent that shall be used is given.
    swap_chain_extent_ = swap_chain_support.capabilities.currentExtent;
  } else {
    // We can choose the extent ourselves.
    int width, height;
    SDL_Vulkan_GetDrawableSize(window_, &width, &height);
    swap_chain_extent_ = {static_cast<u32>(width), static_cast<u32>(height)};
    swap_chain_extent_.width = clamp(swap_chain_extent_.width, swap_chain_support.capabilities.minImageExtent.width, swap_chain_support.capabilities.maxImageExtent.width);
    swap_chain_extent_.height = clamp(swap_chain_extent_.height, swap_chain_support.capabilities.minImageExtent.height, swap_chain_support.capabilities.maxImageExtent.height);
  }
  
  // Decide on the number of images in the swap chain.
  // Policy: If VK_PRESENT_MODE_MAILBOX_KHR is used, use triple buffering:
  // Ideally, one frame is used for display, while one is being rendered to, and
  // the third frame is ready for display. Thus, the frame which is ready can be
  // updated until it is put on display. Con: frames may be rendered and never
  // shown; It is not clear which frame will be displayed in the end, so it is
  // not clear at which time the application state should be rendered if
  // something is moving, so movements will not be completely fluid. If
  // VK_PRESENT_MODE_FIFO_KHR is used, also use triple buffering as advised by
  // the Vulkan validation layers.
  // TODO: We changed the image count from 3 to 2 to reduce the rendering latency.
  //       However, the validation layers complain about that being suboptimal.
  //       Would it be preferable to allocate three images, but only create a maximum
  //       of two frames-in-flight at the same time, and also wait with the rendering
  //       to prevent all three images getting rendered to before the first one was displayed?
  u32 image_count = 2;
  image_count = max(image_count, swap_chain_support.capabilities.minImageCount);
  if (swap_chain_support.capabilities.maxImageCount > 0 &&
      image_count > swap_chain_support.capabilities.maxImageCount) {
    image_count = swap_chain_support.capabilities.maxImageCount;
  }
  
  // Create the swap chain.
  VkSwapchainCreateInfoKHR swap_chain_create_info = {};
  swap_chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swap_chain_create_info.surface = surface_;
  swap_chain_create_info.minImageCount = image_count;
  swap_chain_create_info.imageFormat = selected_surface_format.format;
  swap_chain_create_info.imageColorSpace = selected_surface_format.colorSpace;
  swap_chain_create_info.imageExtent = swap_chain_extent_;
  swap_chain_create_info.imageArrayLayers = 1;
  // Potentially use VK_IMAGE_USAGE_TRANSFER_DST_BIT here if the scene is
  // rendered to a different image first and then transferred to the output:
  swap_chain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  
  u32 shared_queue_family_indices[] = {static_cast<u32>(device_.graphics_queue_family_index()),
                                       static_cast<u32>(device_.presentation_queue_family_index())};
  if (device_.graphics_queue_family_index() != device_.presentation_queue_family_index()) {
    // NOTE: VK_SHARING_MODE_EXCLUSIVE would also be possible using explicit
    //       ownership transfers.
    swap_chain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swap_chain_create_info.queueFamilyIndexCount = 2;
    swap_chain_create_info.pQueueFamilyIndices = shared_queue_family_indices;
  } else {
    swap_chain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_chain_create_info.queueFamilyIndexCount = 0;
    swap_chain_create_info.pQueueFamilyIndices = nullptr;
  }
  
  // Do not use any special transforms. At the time of writing (January 2017),
  // the potentially available transformations are rotation and mirroring of
  // the image.
  swap_chain_create_info.preTransform = swap_chain_support.capabilities.currentTransform;
  // Do not use the alpha channel to blend with other windows in the window
  // system.
  swap_chain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swap_chain_create_info.presentMode = selected_present_mode;
  // Do not care about the colors of pixels that are obscured (for example by
  // other windows in the window system).
  swap_chain_create_info.clipped = VK_TRUE;
  swap_chain_create_info.oldSwapchain = old_swap_chain;
  
  if (api.vkCreateSwapchainKHR(device_.device(), &swap_chain_create_info, nullptr, &swap_chain_) != VK_SUCCESS) {
    // TODO: The reason for retrying up to a few times here is that sometimes, on resizing the window,
    //       according to the Vulkan debug layers, we try to create a swap chain with an invalid extent.
    //       It seems as if the extent that we retrieve at the top of CreateSurfaceDependentObjects() is
    //       out of date in these cases. Perhaps the window system asynchronously resizes the window in the background?
    //       If so, is there a way for us to detect that and handle this in a better way than by simply retrying
    //       and hoping for it to work eventually?
    //
    //       Seems like there might be an inherent race condition here:
    //       https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/624
    //       "The window system can resize the native surface at any time between when the size is queried and when the swapchain is effectively created."
    -- remainingAttempts;
    if (remainingAttempts > 0) {
      LOG(ERROR) << "Swap chain creation failed, retrying.";
      goto retry;
    } else {
      LOG(ERROR) << "Swap chain creation failed, aborting.";
      return false;
    }
  }
  
  if (old_swap_chain != VK_NULL_HANDLE) {
    api.vkDestroySwapchainKHR(device_.device(), old_swap_chain, nullptr);
  }
  
  // Get the handles of the images in the swap chain.
  // NOTE: image_count passed into swap_chain_create_info.minImageCount only
  //       specifies the minimum image count. More images could have been
  //       actually created.
  api.vkGetSwapchainImagesKHR(device_.device(), swap_chain_, &image_count, nullptr);
  VLOG(1) << "Swap chain image count: " << image_count;
  swap_chain_images_.resize(image_count);
  api.vkGetSwapchainImagesKHR(device_.device(), swap_chain_, &image_count, swap_chain_images_.data());
  
  // Create an image view for each image in the swap chain.
  swap_chain_image_views_.resize(swap_chain_images_.size());
  for (usize i = 0; i < swap_chain_image_views_.size(); ++ i) {
    if (!swap_chain_image_views_[i].Initialize(VK_IMAGE_ASPECT_COLOR_BIT, swap_chain_images_[i], selected_surface_format.format, /*mip_levels*/ 1, /*layer_count*/ 1, device_)) {
      LOG(ERROR) << "Failed to create an image view for a swap chain image.";
      return false;
    }
  }
  
  // If MSAA is enabled, create an image holding the MSAA samples
  if (msaa_samples_ != VK_SAMPLE_COUNT_1_BIT) {
    msaa_image_.create_info().samples = msaa_samples_;
    if (!msaa_image_.Initialize(swap_chain_extent_.width, swap_chain_extent_.height, selected_surface_format.format, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, device_)) {
      LOG(ERROR) << "Failed to allocate msaa image";
      return false;
    }
    
    if (!msaa_image_view_.Initialize(VK_IMAGE_ASPECT_COLOR_BIT, msaa_image_)) {
      LOG(ERROR) << "Failed to allocate msaa image view";
      return false;
    }
  }
  
  // Create the depth buffer.
  // Find a suitable depth format
  // Note: Since we do not use the stencil buffer, we prefer formats without a stencil buffer.
  // We also prefer 24 bits for depth, since this is supposed to be faster ( at least for Nvidia
  // graphics cards at the time of writing: https://devblogs.nvidia.com/vulkan-dos-donts/ ).
  vector<VkFormat> depth_formats = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT};
  VkFormat depth_format;
  if (!device_.FindFormatWithOptimalTiling(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, depth_formats, &depth_format)) {
    LOG(ERROR) << "Failed to find a valid depth format.";
    return false;
  }
  
  depth_stencil_image_.create_info().samples = msaa_samples_;
  if (!depth_stencil_image_.Initialize(
      swap_chain_extent_.width,
      swap_chain_extent_.height,
      depth_format,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      device_)) {
    LOG(ERROR) << "Failed to allocate the depth/stencil image.";
    return false;
  }
  if (!depth_stencil_image_view_.Initialize(VK_IMAGE_ASPECT_DEPTH_BIT | ((VulkanFormatGetStencilBits(depth_format) > 0) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0), depth_stencil_image_)) {
    LOG(ERROR) << "Failed to allocate the depth/stencil image view.";
    return false;
  }
  
  // Create render pass.
  VulkanRenderSubPass subpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
  subpass.AddColorAttachment(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  subpass.AddDepthStencilAttachment(1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  if (msaa_samples_ != VK_SAMPLE_COUNT_1_BIT) {
    subpass.AddResolveAttachment(2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
  
  render_pass_.AddColorAttachment(
      selected_surface_format.format,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      (msaa_samples_ == VK_SAMPLE_COUNT_1_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      msaa_samples_);
  render_pass_.AddDepthStencilAttachment(
      depth_format,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      msaa_samples_);
  if (msaa_samples_ != VK_SAMPLE_COUNT_1_BIT) {
    // If msaa is used, add the msaa resolve attachment at index 2
    // (this is a 1-sample color buffer that gets the final resolved color image).
    render_pass_.AddColorAttachment(
        selected_surface_format.format,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_SAMPLE_COUNT_1_BIT);
  }
  render_pass_.AddSubpass(&subpass);
  // render_pass_.AddSubpassDependency(
  //     VK_SUBPASS_EXTERNAL,
  //     0,
  //     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  //     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  //     0,
  //     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
  //     0);
  // render_pass_.AddSubpassDependency(
  //     VK_SUBPASS_EXTERNAL,
  //     0,
  //     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  //     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  //     0,
  //     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  //     VK_DEPENDENCY_BY_REGION_BIT);
  // TODO: Can we use the separate dependencies above instead? See: https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/2088
  render_pass_.AddSubpassDependency(
      VK_SUBPASS_EXTERNAL,
      0,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      0);
  if (!render_pass_.Initialize(device_)) {
    LOG(ERROR) << "Failed to create a render pass.";
    return false;
  }
  
  // Create a framebuffer for each swap chain image to be able to bind it as an
  // attachment to the render pass.
  swap_chain_framebuffers_.resize(swap_chain_image_views_.size());
  for (usize i = 0; i < swap_chain_image_views_.size(); ++ i) {
    VulkanFramebuffer& framebuffer = swap_chain_framebuffers_[i];
    
    if (msaa_samples_ == VK_SAMPLE_COUNT_1_BIT) {
      framebuffer.AddAttachment(swap_chain_image_views_[i]);
      framebuffer.AddAttachment(depth_stencil_image_view_);
    } else {
      framebuffer.AddAttachment(msaa_image_view_);
      framebuffer.AddAttachment(depth_stencil_image_view_);
      framebuffer.AddAttachment(swap_chain_image_views_[i]);
    }
    
    if (!framebuffer.Initialize(swap_chain_extent_.width, swap_chain_extent_.height, /*layers*/ 1, device_, render_pass_)) {
      LOG(ERROR) << "Failed to create a framebuffer.";
      return false;
    }
  }
  
  return callbacks_->InitializeSurfaceDependent();
}

void RenderWindowSDLVulkan::DestroySurfaceDependentObjects() {
  msaa_image_view_.Destroy();
  msaa_image_.Destroy();
  depth_stencil_image_view_.Destroy();
  depth_stencil_image_.Destroy();
  swap_chain_framebuffers_.clear();
  render_pass_.Destroy();
  swap_chain_image_views_.clear();
}

}

#endif
