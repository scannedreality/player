#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/openxr/openxr_vulkan_application.hpp"

#include <array>
#include <thread>

#ifdef __ANDROID__
  #include <android/log.h>
#endif

#include "scan_studio/viewer_common/pi.hpp"
#include <sophus/se3.hpp>

#include <loguru.hpp>

#include <libvis/vulkan/physical_device.h>
#include <libvis/vulkan/queue.h>

namespace scan_studio {

bool OpenXRVulkanApplication::Initialize(
    const char* applicationName,
    XrVersion openXRVersion,
    u32 suggestedVulkanVersion,
    bool useOpenXRDebugLayers,
    bool useVulkanDebugLayers,
    const shared_ptr<OpenXRApplicationCallbacks>& appCallbacks) {
  this->appCallbacks = appCallbacks;
  appCallbacks->SetApplication(this);
  
  // Initialize an OpenXR instance and system, and get the requirements for the Vulkan instance to use.
  if (!InitializeOpenXRInstance(applicationName, openXRVersion, useOpenXRDebugLayers)) { return false; }
  
  // Initialize a Vulkan instance, following the OpenXR instance's requirements.
  if (!InitializeVulkanInstance(suggestedVulkanVersion, useVulkanDebugLayers)) { return false; }
  
  // Get the OpenXR instance's requirements on the Vulkan device and initialize it.
  if (!InitializeVulkanDevice()) { return false; }
  
  // Create the OpenXR session with Vulkan graphics binding,
  // create the OpenXR space, and select the view configuration.
  if (!InitializeOpenXRSessionSpaceAndViewConfig()) { return false; }
  
  // Create the render pass, views, and swap chains.
  if (!InitializeRenderPassAndSwapChains()) { return false; }
  
  // Create command pool, command buffers, and possibly other graphic objects.
  if (!InitializeGraphicObjects()) { return false; }
  
  // Specify composition layers
  if (!InitializeCompositionLayers()) { return false; }
  
  // Let the application do its own initializations
  if (!appCallbacks->Initialize()) { return false; }
  
  // Query the initial interaction profiles
  appCallbacks->OnCurrentInteractionProfilesChanged();
  
  return true;
}

#ifdef __ANDROID__
static void AndroidAppHandleCmd(struct android_app* app, int32_t cmd) {
  bool* appIsResumed = reinterpret_cast<bool*>(app->userData);
  
  switch (cmd) {
    // There is no APP_CMD_CREATE. The ANativeActivity creates the
    // application thread from onCreate(). The application thread
    // then calls android_main().
    case APP_CMD_START: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_START");
      break;
    }
    case APP_CMD_RESUME: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_RESUME");
      *appIsResumed = true;
      break;
    }
    case APP_CMD_PAUSE: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_PAUSE");
      *appIsResumed = false;
      break;
    }
    case APP_CMD_STOP: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_STOP");
      break;
    }
    case APP_CMD_DESTROY: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_DESTROY");
      // appState->NativeWindow = NULL;
      break;
    }
    case APP_CMD_INIT_WINDOW: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_INIT_WINDOW");
      // appState->NativeWindow = app->window;
      break;
    }
    case APP_CMD_TERM_WINDOW: {
      __android_log_print(ANDROID_LOG_INFO, "XRTubeQuest", "%s", "APP_CMD_TERM_WINDOW");
      // appState->NativeWindow = NULL;
      break;
    }
  }
}
#endif

void OpenXRVulkanApplication::RunMainLoop(
    #ifdef __ANDROID__
      struct android_app* androidApp
    #endif
) {
  if (sceneCallbacks == nullptr && nextSceneCallbacks == nullptr) {
    LOG(ERROR) << "SetScene() must be used to set a scene before calling RunMainLoop()";
    return;
  }
  
  #ifdef __ANDROID__
    bool appIsResumed = false;
    androidApp->userData = &appIsResumed;
    androidApp->onAppCmd = AndroidAppHandleCmd;
  #endif
  
  quitRequested = false;
  bool xrRunning = false;
  xrSessionState = XR_SESSION_STATE_UNKNOWN;
  
  while (!quitRequested) {
    // Switch the current scene?
    if (nextSceneCallbacks) {
      if (sceneCallbacks) {
        sceneCallbacks->OnLeaveScene();
      }
      
      sceneCallbacks = nextSceneCallbacks;
      nextSceneCallbacks = nullptr;
      
      sceneCallbacks->OnEnterScene();
    }
    
    // Handle Android events
    #ifdef __ANDROID__
      for (;;) {
        int events;
        struct android_poll_source* source;
        // If the timeout is zero, returns immediately without blocking.
        // If the timeout is negative, waits indefinitely until an event appears.
        const int timeoutMilliseconds = (!appIsResumed && !xrRunning && !quitRequested) ? -1 : 0;
        if (ALooper_pollAll(timeoutMilliseconds, NULL, &events, (void**)&source) < 0) {
          break;
        }
        
        // Process this event.
        if (source != NULL) {
          source->process(androidApp, source);
        }
      }
    #endif
    
    // Poll OpenXR events
    XrEventDataBuffer eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(xrInstance, &eventBuffer) == XR_SUCCESS) {
      switch (eventBuffer.type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        XrEventDataSessionStateChanged* event = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventBuffer);
        xrSessionState = event->state;
        
        // React to certain session state changes
        switch (xrSessionState) {
        case XR_SESSION_STATE_READY: {
          XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
          beginInfo.primaryViewConfigurationType = viewConfigurationType;
          if (!XrCheckResult(xrBeginSession(xrSession, &beginInfo))) { LOG(ERROR) << "xrBeginSession() failed"; }
          xrRunning = true;
        } break;
        case XR_SESSION_STATE_STOPPING:
          xrRunning = false;
          if (!XrCheckResult(xrEndSession(xrSession))) { LOG(ERROR) << "xrEndSession() failed"; }
          break;
        case XR_SESSION_STATE_EXITING:
          quitRequested = true;
          break;
        case XR_SESSION_STATE_LOSS_PENDING:
          quitRequested = true;
          break;
        default:;
        }
      } break;
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
        quitRequested = true;
        break;
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
        appCallbacks->OnCurrentInteractionProfilesChanged();
        break;
      default:;
      }
      eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    }
    if (quitRequested) {
      break;
    }
    
    if (xrRunning) {
      RenderFrame();
      
      if (xrSessionState != XR_SESSION_STATE_VISIBLE &&
          xrSessionState != XR_SESSION_STATE_FOCUSED) {
        this_thread::sleep_for(chrono::milliseconds(250));
      }
    }
  }
  
  sceneCallbacks->OnLeaveScene();
  
  if (!VulkanCheckResult(vkDevice.Api().vkDeviceWaitIdle(vkDevice))) {
    LOG(ERROR) << "Failed to wait for the device to become idle";
  }
  
  // Release callbacks such that they can be destructed before the OpenXRVulkanApplication
  nextSceneCallbacks.reset();
  sceneCallbacks.reset();
  appCallbacks.reset();
}

void OpenXRVulkanApplication::Quit() {
  quitRequested = true;
}

void OpenXRVulkanApplication::SetScene(const shared_ptr<OpenXRSceneCallbacks>& callbacks) {
  nextSceneCallbacks = callbacks;
  nextSceneCallbacks->SetApplication(this, appCallbacks.get());
}

void OpenXRVulkanApplication::AddCompositionLayer(int insertionIndex, XrCompositionLayerBaseHeader* layer) {
  compositionLayers.insert(compositionLayers.begin() + insertionIndex, layer);
}

void OpenXRVulkanApplication::RemoveCompositionLayer(XrCompositionLayerBaseHeader* layer) {
  for (auto it = compositionLayers.begin(); it != compositionLayers.end(); ++ it) {
    if (*it == layer) {
      compositionLayers.erase(it);
      return;
    }
  }
  
  LOG(ERROR) << "Composition layer to remove not found: " << layer;
}

bool OpenXRVulkanApplication::InitializeOpenXRInstance(const char* applicationName, XrVersion version, bool useOpenXRDebugLayers) {
  vector<string> xrInstanceExtensions;
  
  if (!appCallbacks->SpecifyOpenXRInstanceExtensions(useOpenXRDebugLayers, &xrInstanceExtensions)) {
    return false;
  }
  
  string missingExtensionOrLayer;
  OpenXRInstance::InitResult initResult = xrInstance.Initialize(applicationName, version, xrInstanceExtensions, useOpenXRDebugLayers, &missingExtensionOrLayer);
  if (initResult != OpenXRInstance::InitResult::Success) {
    ostringstream errorMsg;
    
    switch (initResult) {
    case OpenXRInstance::InitResult::InstanceUnavailable:
      errorMsg << "OpenXR instance unavailable. Please connect your XR headset before starting the application.\n"
                  "If this error still occurs with the headset connected and active, then please ensure that an OpenXR runtime is installed.";
      break;
    case OpenXRInstance::InitResult::FormFactorUnavailable:
      errorMsg << "OpenXR device unavailable, the XR device is not connected or not ready. Please connect your XR headset before starting the application.";
      break;
    case OpenXRInstance::InitResult::Error:
      errorMsg << "There was an error during OpenXR initialization.";
      break;
    case OpenXRInstance::InitResult::Rejected:
      errorMsg << "The OpenXR runtime does not support the extension or layer \"" << missingExtensionOrLayer << "\" required by this application.";
      if (missingExtensionOrLayer == "XR_KHR_vulkan_enable") {
        errorMsg << "\n\nIf you are using a Windows Mixed Reality (WMR) headset with WMR's OpenXR runtime, then you can fix this error by switching to SteamVR's OpenXR runtime.\n"
                    "To do so, start SteamVR, open its settings via the menu button on the SteamVR status window, go to \"Developer\", and click \"Set SteamVR as OpenXR runtime\".";
      }
      break;
    case OpenXRInstance::InitResult::Success: break;
    }
    
    return OnError(errorMsg.str());
  }
  
  return true;
}

bool OpenXRVulkanApplication::InitializeVulkanInstance(u32 suggestedVersion, bool useVulkanDebugLayers) {
  vector<string> vkInstanceExtensions = xrInstance.GetRequiredVulkanInstanceExtensions();
  
  LOG(1) << "List of instance extensions requested by the OpenXR runtime:";
  for (const string& extension : vkInstanceExtensions) {
    LOG(1) << "- " << extension;
  }
  
  if (useVulkanDebugLayers) {
    LOG(WARNING) << "Enabling additional Vulkan debug layers!";
    vkInstanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
  }
  
  array<VkValidationFeatureEnableEXT, 3> validationFeatureEnable = {
      VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
      // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
      // VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,  // Cannot be used at the same time as VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
  
  VkValidationFeaturesEXT validationFeatures = {VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
  if (useVulkanDebugLayers) {
    validationFeatures.enabledValidationFeatureCount = validationFeatureEnable.size();
    validationFeatures.pEnabledValidationFeatures = validationFeatureEnable.data();
  }
  
  vulkanVersion = min<u32>(max<u32>(suggestedVersion, xrInstance.GetMinSupportedVulkanVersion()), xrInstance.GetMaxSupportedVulkanVersion());
  if (!vkInstance.Initialize(vulkanVersion, vkInstanceExtensions, useVulkanDebugLayers, useVulkanDebugLayers ? &validationFeatures : nullptr)) {
    ostringstream errorMsg;
    errorMsg << "Failed to initialize a Vulkan " << VK_VERSION_MAJOR(vulkanVersion) << "." << VK_VERSION_MINOR(vulkanVersion) << "." << VK_VERSION_PATCH(vulkanVersion) << " instance."
                " Please make sure that your graphics drivers are up-to-date and your GPU supports this version of Vulkan.";
    return OnError(errorMsg.str());
  }
  
  return true;
}

bool OpenXRVulkanApplication::InitializeVulkanDevice() {
  VkPhysicalDevice vkPhysicalDeviceRaw;
  vector<string> vkDeviceExtensions;
  
  if (!xrInstance.GetVulkanDeviceToUse(vkInstance, &vkPhysicalDeviceRaw, &vkDeviceExtensions)) {
    return OnError("Failed to query the Vulkan device requirements of the OpenXR instance.");
  }
  vkPhysicalDevice.reset(new VulkanPhysicalDevice(vkPhysicalDeviceRaw, vkInstance));
  
  LOG(1) << "List of device extensions requested by the OpenXR runtime:";
  for (const string& extension : vkDeviceExtensions) {
    LOG(1) << "- " << extension;
  }
  
  selected_graphics_queue_family_index = vkPhysicalDevice->FindQueueFamily(VK_QUEUE_GRAPHICS_BIT);
  
  // Create a logical device for the selected physical device.
  vkDevice.RequestQueues(selected_graphics_queue_family_index, /*count*/ 1);
  vkDevice.SetGraphicsQueueFamilyIndex(selected_graphics_queue_family_index);
  
  VkPhysicalDeviceFeatures enabled_features{};
  const void* deviceCreateInfoPNext = nullptr;
  if (!appCallbacks->SpecifyVulkanDeviceFeaturesAndExtensions(*vkPhysicalDevice, &enabled_features, &vkDeviceExtensions, &deviceCreateInfoPNext)) {
    return false;
  }
  if (!vkDevice.Initialize(*vkPhysicalDevice, enabled_features, vkDeviceExtensions, vkInstance, deviceCreateInfoPNext)) {
    return OnError("Failed to initialize a Vulkan device. Please make sure that your graphics drivers are up-to-date.");
  }
  
  xrQueueIndex = 0;
  graphicsQueue = vkDevice.GetQueue(selected_graphics_queue_family_index, /*queue_index*/ xrQueueIndex);
  
  return true;
}

bool OpenXRVulkanApplication::InitializeOpenXRSessionSpaceAndViewConfig() {
  if (!xrSession.Initialize(xrInstance, selected_graphics_queue_family_index, xrQueueIndex, vkDevice, vkInstance)) {
    return OnError("Failed to create OpenXR session: No XR device attached or not ready. Please connect your XR headset before starting the application.");
  }
  
  // Let the callbacks choose the space and view types
  const auto& supportedReferenceSpaces = xrSession.GetSupportedReferenceSpaces();
  const auto& supportedViewConfigurations = xrInstance.GetSupportedViewConfigurations();
  
  XrReferenceSpaceType chosenReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;  // must be supported by all runtimes
  viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;
  if (!appCallbacks->ChooseSpaceAndView(supportedReferenceSpaces, &chosenReferenceSpaceType, supportedViewConfigurations, &viewConfigurationType)) {
    return false;
  }
  
  // Create the chosen reference space (and the view space)
  string chosenRefSpaceName;
  switch (chosenReferenceSpaceType) {
  case XR_REFERENCE_SPACE_TYPE_VIEW: chosenRefSpaceName = "view"; break;
  case XR_REFERENCE_SPACE_TYPE_LOCAL: chosenRefSpaceName = "local"; break;
  case XR_REFERENCE_SPACE_TYPE_STAGE: chosenRefSpaceName = "stage"; break;
  case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: chosenRefSpaceName = "unbounded (msft)"; break;
  default: chosenRefSpaceName = "unknown"; break;
  }
  LOG(INFO) << "Chosen reference space type: " << chosenRefSpaceName;
  
  if (!xrSpace.Initialize(chosenReferenceSpaceType, xrSession)) {
    ostringstream errorMsg;
    errorMsg << "Failed to create a(n) " << chosenRefSpaceName << " OpenXR reference space.";
    return OnError(errorMsg.str());
  }
  
  if (!xrViewSpace.Initialize(XR_REFERENCE_SPACE_TYPE_VIEW, xrSession)) {
    return OnError("Failed to create an OpenXR view space.");
  }
  
  // Verify that the view configuration type returned by the callback is actually supported
  bool viewConfigurationTypeFound = false;
  for (const XrViewConfigurationType& type : supportedViewConfigurations) {
    if (type == viewConfigurationType) {
      viewConfigurationTypeFound = true;
      break;
    }
  }
  
  if (!viewConfigurationTypeFound) {
    return OnError("Failed to select a supported view configuration type.");
  }
  
  // Enumerate blend modes
  // TODO: Currently, we do not use the result. Where should it be used?
  // const auto supportedBlendModes = xrInstance.GetSupportedBlendModes(viewConfigurationType);
  // if (supportedBlendModes.empty()) {
  //   LOG(ERROR) << "Failed to enumerate supported blend modes from OpenXR.";
  //   return false;
  // }
  
  return true;
}

bool OpenXRVulkanApplication::InitializeRenderPassAndSwapChains() {
  VkSampleCountFlagBits msaaSamples;
  appCallbacks->ChooseMsaaSamples(*vkPhysicalDevice, &msaaSamples);
  
  // Enumerate swap chain formats
  const vector<s64> swapchainFormats = xrSession.EnumerateSwapchainFormats();
  if (swapchainFormats.empty()) {
    return OnError("Failed to enumerate swap chain formats from OpenXR.");
  }
  
  for (s64 format : swapchainFormats) {
    LOG(1) << "Supported swap chain format: " << VulkanGetFormatName(static_cast<VkFormat>(format));
  }
  
  // Choose a swap chain format.
  // The formats are returned in order from highest to lowest preference by the OpenXR runtime.
  // Therefore, in case we do not find our preferred format, simply choose the first one that is of suitable type.
  // TODO: We should let the callbacks object determine / modify the chosen format.
  const VkFormat preferredSwapChainFormat = VK_FORMAT_R8G8B8A8_SRGB;
  VkFormat chosenSwapChainFormat = VK_FORMAT_UNDEFINED;
  
  const VkFormat preferredDepthSwapChainFormat = VK_FORMAT_D32_SFLOAT;
  VkFormat chosenDepthSwapChainFormat = VK_FORMAT_UNDEFINED;
  
  for (s64 format : swapchainFormats) {
    if (static_cast<VkFormat>(format) == preferredSwapChainFormat) {
      chosenSwapChainFormat = preferredSwapChainFormat;
    } else if (chosenSwapChainFormat == VK_FORMAT_UNDEFINED && !VulkanIsDepthStencilFormat(static_cast<VkFormat>(format))) {
      chosenSwapChainFormat = static_cast<VkFormat>(format);
    }
    
    if (static_cast<VkFormat>(format) == preferredDepthSwapChainFormat) {
      chosenDepthSwapChainFormat = preferredDepthSwapChainFormat;
    } else if (chosenDepthSwapChainFormat == VK_FORMAT_UNDEFINED && VulkanIsDepthStencilFormat(static_cast<VkFormat>(format))) {
      chosenDepthSwapChainFormat = static_cast<VkFormat>(format);
    }
  }
  
  LOG(INFO) << "Chosen color swap chain format: " << VulkanGetFormatName(chosenSwapChainFormat);
  LOG(INFO) << "Chosen depth swap chain format: " << VulkanGetFormatName(chosenDepthSwapChainFormat);
  
  isSRGBRenderTargetUsed = VulkanIsSRGBFormat(chosenSwapChainFormat);
  if (!isSRGBRenderTargetUsed) {
    // In case we get this error and no sRGB render target is available, we must implement the color space conversion from linear to sRGB ourselves.
    LOG(ERROR) << "No SRGB render target chosen - colors will be output incorrectly!";
  }
  
  // Create render pass
  VulkanRenderSubPass subpass(VK_PIPELINE_BIND_POINT_GRAPHICS);
  subpass.AddColorAttachment(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  subpass.AddDepthStencilAttachment(1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
    subpass.AddResolveAttachment(2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
  
  renderPass.AddColorAttachment(
      chosenSwapChainFormat,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      (msaaSamples == VK_SAMPLE_COUNT_1_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      msaaSamples);
  renderPass.AddDepthStencilAttachment(
      chosenDepthSwapChainFormat,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      usingExtension_XR_KHR_composition_layer_depth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      msaaSamples);
  if (msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
    // If msaa is used, add the msaa resolve attachment at index 2
    // (this is a 1-sample color buffer that gets the final resolved color image).
    renderPass.AddColorAttachment(
        chosenSwapChainFormat,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT);
  }
  renderPass.AddSubpass(&subpass);
  // renderPass.AddSubpassDependency(
  //     VK_SUBPASS_EXTERNAL,
  //     0,
  //     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  //     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  //     0,
  //     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
  //     0);
  // renderPass.AddSubpassDependency(
  //     VK_SUBPASS_EXTERNAL,
  //     0,
  //     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  //     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  //     0,
  //     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  //     VK_DEPENDENCY_BY_REGION_BIT);
  // TODO: Can we use the separate dependencies above instead? See: https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/2088
  renderPass.AddSubpassDependency(
      VK_SUBPASS_EXTERNAL,
      0,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      0);
  if (!renderPass.Initialize(vkDevice)) {
    return OnError("Failed to create a Vulkan render pass.");
  }
  
  // Enumerate the views within the chosen view configuration (these are the "view configuration views" represented by XrViewConfigurationView)
  vector<XrViewConfigurationView> xrConfigViews = xrInstance.EnumerateViews(viewConfigurationType);
  if (xrConfigViews.empty()) {
    return OnError("Failed to enumerate OpenXR view configuration views.");
  }
  
  // Create a swap chain for each view
  xrSwapchains.resize(xrConfigViews.size());
  for (u32 viewIndex = 0; viewIndex < xrConfigViews.size(); ++ viewIndex) {
    const XrViewConfigurationView& view = xrConfigViews[viewIndex];
    
    if (!xrSwapchains[viewIndex].Initialize(chosenSwapChainFormat, msaaSamples, chosenDepthSwapChainFormat, !usingExtension_XR_KHR_composition_layer_depth, view, xrSession, renderPass, vkDevice)) {
      return OnError("Failed to create an OpenXR swapchain.");
    }
  }
  
  xrViews.resize(xrConfigViews.size(), {XR_TYPE_VIEW});
  projViews.resize(xrConfigViews.size());
  depthViews.resize(xrConfigViews.size());
  
  // Each frame, we have to wait for each swap chain image that will be used in that frame to be ready
  // (i.e., not being read by the compositor anymore). This function appears to block CPU execution, judging
  // from its description in the OpenXR specificaiton, rather than only inserting a wait command on the GPU side.
  // We record the command buffers for the current frame before this wait, so we have to consider one more.
  // Therefore, the maximum number of frames in flight (which is important to know how many per-frame GPU
  // resources to allocate) is:
  //   min(swap chain image count) + 1.
  maxFrameInFlightCount = xrSwapchains[0].GetImageCount() + 1;
  currentFrameInFlightIndex = 0;
  
  return true;
}

bool OpenXRVulkanApplication::InitializeGraphicObjects() {
  // Allocate command pool
  if (!graphicsCommandPool.Initialize(vkDevice.graphics_queue_family_index(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, vkDevice)) {
    return OnError("Failed to allocate a Vulkan command pool");
  }
  
  // Allocate command buffers
  commandBuffers.resize(maxFrameInFlightCount);
  for (usize i = 0; i < commandBuffers.size(); ++ i) {
    if (!commandBuffers[i].Initialize(VK_COMMAND_BUFFER_LEVEL_PRIMARY, graphicsCommandPool)) {
      return OnError("Failed to allocate a Vulkan command buffer");
    }
  }
  
  return true;
}

bool OpenXRVulkanApplication::InitializeCompositionLayers() {
  // Configure the application's render layer
  applicationLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  applicationLayer.space = xrSpace;
  applicationLayer.viewCount = projViews.size();
  applicationLayer.views = projViews.data();
  
  // Set the application's render layer as the only composition layer (for now)
  compositionLayers = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&applicationLayer)};
  
  return true;
}

void OpenXRVulkanApplication::RenderFrame() {
  // Throttle the application frame loop in order to synchronize application frame submissions with the display.
  XrFrameState frameState = {XR_TYPE_FRAME_STATE};
  XR_CHECKED_CALL(xrWaitFrame(xrSession, nullptr, &frameState));
  
  if (xrSessionState == XR_SESSION_STATE_FOCUSED) {
    sceneCallbacks->PollActions(frameState.predictedDisplayTime);
  }
  
  graphicsQueue->Lock();  // xrBeginFrame() may use this queue, see "Concurrency" in XR_KHR_vulkan_enable
  XR_CHECKED_CALL(xrBeginFrame(xrSession, nullptr));
  graphicsQueue->Unlock();
  
  bool layerRendered = false;
  
  if (frameState.shouldRender == XR_TRUE) {
    // Execute any code that is dependent on the predicted time.
    // TODO?
    
    // If the session is active, render our layer.
    const bool sessionActive = xrSessionState == XR_SESSION_STATE_VISIBLE || xrSessionState == XR_SESSION_STATE_FOCUSED;
    if (sessionActive && RenderLayer(frameState.predictedDisplayTime)) {
      layerRendered = true;
    }
  }
  
  XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
  endInfo.displayTime = frameState.predictedDisplayTime;
  endInfo.environmentBlendMode = environmentBlendMode;
  endInfo.layerCount = layerRendered ? compositionLayers.size() : 0;
  endInfo.layers = layerRendered ? compositionLayers.data() : nullptr;
  graphicsQueue->Lock();  // xrEndFrame() may use this queue, see "Concurrency" in XR_KHR_vulkan_enable
  XR_CHECKED_CALL(xrEndFrame(xrSession, &endInfo));
  graphicsQueue->Unlock();
}

bool OpenXRVulkanApplication::RenderLayer(XrTime predictedDisplayTime) {
  const auto& vkApi = vkDevice.Api();
  
  // Proceed to the next frame-in-flight resources.
  currentFrameInFlightIndex = (currentFrameInFlightIndex + 1) % maxFrameInFlightCount;
  
  // Begin the command buffer.
  VulkanCommandBuffer& cmdBuf = commandBuffers.at(currentFrameInFlightIndex);
  if (!VulkanCheckResult(vkApi.vkResetCommandBuffer(cmdBuf, /*flags*/ 0))) { LOG(ERROR) << "Failed to reset a command buffer"; return false; }
  if (!cmdBuf.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) { LOG(ERROR) << "Failed to Begin() a command buffer"; return false; }
  
  // Record the command buffer.
  float nearZ, farZ;
  sceneCallbacks->PrepareFrame(&cmdBuf, predictedDisplayTime, &nearZ, &farZ);
  
  // For each view, we acquire a swap chain image and render the view.
  // Note that we do not wait for the swap chain images yet.
  for (u32 viewIndex = 0; viewIndex < xrSwapchains.size(); ++ viewIndex) {
    OpenXRSwapchain& swapchain = xrSwapchains[viewIndex];
    
    // Query a swapchain color + depth image pair to use for rendering
    u32 colorSwapchainImageIndex, depthSwapchainImageIndex = 0;
    graphicsQueue->Lock();  // xrAcquireSwapchainImage() may use this queue, see "Concurrency" in XR_KHR_vulkan_enable
    if (!XrCheckResult(xrAcquireSwapchainImage(swapchain.GetColorSwapchain(), nullptr, &colorSwapchainImageIndex))) { graphicsQueue->Unlock(); return false; }
    if (usingExtension_XR_KHR_composition_layer_depth &&
        !XrCheckResult(xrAcquireSwapchainImage(swapchain.GetDepthSwapchain(), nullptr, &depthSwapchainImageIndex))) { graphicsQueue->Unlock(); return false; }
    graphicsQueue->Unlock();
    
    // Record rendering commands for this view
    // TODO: Can we use optimizations since we are essentially rendering the same scene twice?
    //       Or can we use any other VR-specific optimizations, like lower-resolution shading in areas that will be strongly distorted anyway due to the lens distortion?
    const VulkanFramebuffer& framebuffer = swapchain.GetSwapChainFramebuffer(colorSwapchainImageIndex, depthSwapchainImageIndex);
    sceneCallbacks->RenderView(&cmdBuf, viewIndex, predictedDisplayTime, framebuffer, swapchain.GetWidth(), swapchain.GetHeight());
  }
  
  // End the command buffer.
  if (!VulkanCheckResult(vkApi.vkEndCommandBuffer(cmdBuf))) {
    return false;
  }
  
  // Wait for all swap chain images to be ready (not being read by the compositor anymore)
  for (u32 viewIndex = 0; viewIndex < xrSwapchains.size(); ++ viewIndex) {
    OpenXRSwapchain& swapchain = xrSwapchains[viewIndex];
    
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (!XrCheckResult(xrWaitSwapchainImage(swapchain.GetColorSwapchain(), &waitInfo))) { return false; }
    if (usingExtension_XR_KHR_composition_layer_depth &&
        !XrCheckResult(xrWaitSwapchainImage(swapchain.GetDepthSwapchain(), &waitInfo))) { return false; }
  }
  
  // Estimate the state and location of each viewpoint at the predicted time.
  // This should be called as late as possible since the prediction might improve as time goes by.
  XrViewState viewState = {XR_TYPE_VIEW_STATE};
  XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
  locateInfo.viewConfigurationType = viewConfigurationType;
  locateInfo.displayTime = predictedDisplayTime;
  locateInfo.space = xrSpace;
  
  u32 viewCount = 0;
  if (!XrCheckResult(xrLocateViews(xrSession, &locateInfo, &viewState, xrViews.size(), &viewCount, xrViews.data()))) {
    return false;
  }
  if (viewCount != xrSwapchains.size()) {
    LOG(WARNING) << "My assumption that viewCount == xrSwapchains->size() is incorrect. The code must be adapted!";
  }
  
  // Update all model-view-projection matrices that are used by the recorded commands with the recent pose prediction.
  sceneCallbacks->SetRenderMatrices(predictedDisplayTime, viewState.viewStateFlags);
  
  // Submit the command buffer.
  VkPipelineStageFlags stage_flags[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.pWaitDstStageMask = stage_flags;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmdBuf.buffer();
  // TODO: Here, we use the same queue as the one that we specified for the OpenXR runtime to use.
  //       Is that a good idea performance-wise? Or should we use a different queue?
  //       (For example, if OpenXR needs to reproject an old frame to the current viewpoint since we
  //       dropped a frame, this should happen in parallel to our rendering. Is that affected by
  //       the queue choice here, or not?)
  graphicsQueue->Lock();
  if (!VulkanCheckResult(vkApi.vkQueueSubmit(*graphicsQueue, 1, &submit_info, VK_NULL_HANDLE))) {
    LOG(ERROR) << "Failed to submit command buffer";
  }
  graphicsQueue->Unlock();
  
  // Release all swap chain images and set up the view output information.
  for (u32 viewIndex = 0; viewIndex < viewCount; ++ viewIndex) {
    OpenXRSwapchain& swapchain = xrSwapchains[viewIndex];
    XrView& xrView = xrViews[viewIndex];
    XrCompositionLayerProjectionView& projView = projViews[viewIndex];
    
    // Set up rendering information for the view
    projView = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    projView.pose = xrView.pose;
    projView.fov = xrView.fov;
    projView.subImage.swapchain = swapchain.GetColorSwapchain();
    // projView.subImage.imageRect.offset = {0, 0};
    projView.subImage.imageRect.extent = {static_cast<s32>(swapchain.GetWidth()), static_cast<s32>(swapchain.GetHeight())};
    
    if (usingExtension_XR_KHR_composition_layer_depth) {
      XrCompositionLayerDepthInfoKHR& depthView = depthViews[viewIndex];
      
      depthView = {XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
      depthView.subImage.swapchain = swapchain.GetDepthSwapchain();
      depthView.subImage.imageRect.extent = projView.subImage.imageRect.extent;
      depthView.minDepth = 0.f;
      depthView.maxDepth = 1.f;
      depthView.nearZ = nearZ;
      depthView.farZ = farZ;
      
      projView.next = &depthView;
    }
    
    // Release the swapchain image
    graphicsQueue->Lock();  // xrReleaseSwapchainImage() may use this queue, see "Concurrency" in XR_KHR_vulkan_enable
    XR_CHECKED_CALL(xrReleaseSwapchainImage(swapchain.GetColorSwapchain(), nullptr));
    if (usingExtension_XR_KHR_composition_layer_depth) {
      XR_CHECKED_CALL(xrReleaseSwapchainImage(swapchain.GetDepthSwapchain(), nullptr));
    }
    graphicsQueue->Unlock();
  }
  
  return true;
}

bool OpenXRVulkanApplication::OnError(const string& errorMsg) {
  LOG(ERROR) << errorMsg;
  appCallbacks->OnError(errorMsg.c_str());
  return false;
}

}

#endif
