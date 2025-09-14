#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/openxr/instance.hpp"

#include <cstring>

#include <loguru.hpp>

namespace scan_studio {

static void ParseSpaceDelimitedExtensionsString(const vector<char>& extensionsString, vector<string>* extensionList) {
  string extensionName;
  for (usize i = 0; i < extensionsString.size(); ++ i) {
    if (extensionsString[i] == 0) {
      break;
    } else if (extensionsString[i] == ' ') {
      extensionList->push_back(extensionName);
      extensionName.clear();
    } else {
      extensionName += extensionsString[i];
    }
  }
  if (!extensionName.empty()) {
    extensionList->push_back(extensionName);
  }
}

OpenXRInstance::OpenXRInstance() {}

OpenXRInstance::OpenXRInstance(OpenXRInstance&& other)
    : instance(std::move(other.instance)),
      functionTable(std::move(other.functionTable)),
      systemId(other.systemId),
      supportedViewConfigurations(std::move(other.supportedViewConfigurations)),
      minSupportedVulkanVersion(other.minSupportedVulkanVersion),
      maxSupportedVulkanVersion(other.maxSupportedVulkanVersion),
      requiredVulkanInstanceExtensions(std::move(other.requiredVulkanInstanceExtensions)),
      debugMessenger(other.debugMessenger) {
  other.instance = XR_NULL_HANDLE;
  other.debugMessenger = XR_NULL_HANDLE;
}

OpenXRInstance& OpenXRInstance::operator=(OpenXRInstance&& other) {
  Destroy();
  
  instance = std::move(other.instance);
  functionTable = std::move(other.functionTable);
  systemId = other.systemId;
  supportedViewConfigurations = std::move(other.supportedViewConfigurations);
  minSupportedVulkanVersion = other.minSupportedVulkanVersion;
  maxSupportedVulkanVersion = other.maxSupportedVulkanVersion;
  requiredVulkanInstanceExtensions = std::move(other.requiredVulkanInstanceExtensions);
  debugMessenger = other.debugMessenger;
  
  other.instance = XR_NULL_HANDLE;
  other.debugMessenger = XR_NULL_HANDLE;
  return *this;
}

OpenXRInstance::~OpenXRInstance() {
  Destroy();
}

bool OpenXRInstance::IsExtensionSupported(const char* extensionName, bool enable_debug_layers) {
  if (!availableInstanceExtensionsQueried || availableInstanceExtensionsQueriedWithDebugLayers != enable_debug_layers) {
    if (!QueryAvailableInstanceExtensions(enable_debug_layers)) { return false; }
  }
  
  for (const XrExtensionProperties& extension_properties : availableInstanceExtensions) {
    if (strncmp(extensionName, extension_properties.extensionName, XR_MAX_EXTENSION_NAME_SIZE) == 0) {
      return true;
    }
  }
  
  return false;
}

static XRAPI_ATTR XrBool32 XRAPI_CALL
DebugInfoUserCallback(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* /*user_data*/) {
  string typeName;
  if (types & XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    typeName = "[GENERAL] ";
  } else if (types & XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    typeName = "[VALIDATION] ";
  } else if (types & XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    typeName = "[PERFORMANCE] ";
  } else if (types & XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) {
    typeName = "[CONFORMANCE] ";
  }
  
  if (severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    LOG(FATAL) << typeName << "OpenXR debug callback (" << msg->messageId << ", " << msg->functionName << "): " << msg->message << endl;
  } else if (severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    LOG(WARNING) << typeName << "OpenXR debug callback (" << msg->messageId << ", " << msg->functionName << "): " << msg->message << endl;
  } else if (severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    LOG(INFO) << typeName << "OpenXR debug callback (" << msg->messageId << ", " << msg->functionName << "): " << msg->message << endl;
  } else {  // if (severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    LOG(1) << typeName << "OpenXR debug callback (" << msg->messageId << ", " << msg->functionName << "): " << msg->message << endl;
  }
  
  // Returning XR_TRUE here will force the calling function to fail
  return XR_FALSE;
}

OpenXRInstance::InitResult OpenXRInstance::Initialize(const char* applicationName, XrVersion version, const vector<string>& extensions, bool enable_debug_layers, string* missingExtensionOrLayer) {
  constexpr bool kVerboseLogging = false;
  if (kVerboseLogging) {
    LOG(1) << "Initializing OpenXR ...";
  }
  
  // List the available instance layers.
  u32 layer_count;
  if (!XrCheckResult(xrEnumerateApiLayerProperties(0, &layer_count, nullptr))) {
    return OpenXRInstance::InitResult::InstanceUnavailable;
  }
  vector<XrApiLayerProperties> availableApiLayers(layer_count, {XR_TYPE_API_LAYER_PROPERTIES});
  if (!XrCheckResult(xrEnumerateApiLayerProperties(layer_count, &layer_count, availableApiLayers.data()))) {
    return OpenXRInstance::InitResult::InstanceUnavailable;
  }
  if (kVerboseLogging && VLOG_IS_ON(1)) {
    VLOG(1) << "Available API layers:";
    for (const XrApiLayerProperties& layer_properties : availableApiLayers) {
      VLOG(1) << "  " << layer_properties.layerName << " (spec version: " << layer_properties.specVersion << ", layer version: " << layer_properties.layerVersion << ")";
    }
  }
  
  // Add the debug layer to the requested layers if enable_debug_layers.
  vector<string> requestedApiLayers;
  constexpr const char* coreValidationLayerName = "XR_APILAYER_LUNARG_core_validation";
  if (enable_debug_layers) {
    requestedApiLayers.push_back(coreValidationLayerName);
  }
  
  // Check whether the requested layers are supported.
  for (const string& requested_layer : requestedApiLayers) {
    bool found = false;
    for (const XrApiLayerProperties& layer_properties : availableApiLayers) {
      if (requested_layer == layer_properties.layerName) {
        found = true;
        break;
      }
    }
    if (!found) {
      LOG(ERROR) << "Requested instance layer is not supported: " << requested_layer;
      if (requested_layer == coreValidationLayerName) {
        LOG(ERROR) << "In constrast to Vulkan, the OpenXR debug layers are not generally available." << endl
                   << "To use them, you need to do the following:" << endl
                   << " - Build the OpenXR SDK from source: https://github.com/KhronosGroup/OpenXR-SDK-Source" << endl
                   << " - Set the XR_API_LAYER_PATH environment variable to (openxr_base)/(build_folder)/src/api_layers" << endl
                   << "See the Readme at: https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/master/src/api_layers";
      }
      if (missingExtensionOrLayer) {
        *missingExtensionOrLayer = requested_layer;
      }
      return InitResult::Rejected;
    }
  }
  
  // List the avilable instance extensions
  if (!availableInstanceExtensionsQueried || availableInstanceExtensionsQueriedWithDebugLayers != enable_debug_layers) {
    if (!QueryAvailableInstanceExtensions(enable_debug_layers)) {
      return InitResult::InstanceUnavailable;
    }
  }
  
  if (kVerboseLogging && VLOG_IS_ON(1)) {
    LOG(1) << "Available instance extensions (given the enabled instance layers):";
    for (const XrExtensionProperties& extensionProperties : availableInstanceExtensions) {
      LOG(1) << "  " << extensionProperties.extensionName << " (extension version: " << extensionProperties.extensionVersion << ")";
    }
  }
  
  // Add the debug extension to the requested extensions if enable_debug_layers.
  vector<string> requested_instance_extensions = extensions;
  requested_instance_extensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
  if (enable_debug_layers) {
    requested_instance_extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  
  // Check whether the requested extensions are supported.
  for (const string& requested_extension : requested_instance_extensions) {
    bool found = false;
    for (const XrExtensionProperties& extension_properties : availableInstanceExtensions) {
      if (requested_extension == extension_properties.extensionName) {
        found = true;
        break;
      }
    }
    if (!found) {
      LOG(ERROR) << "Requested instance extension is not supported: " << requested_extension;
      if (missingExtensionOrLayer) {
        *missingExtensionOrLayer = requested_extension;
      }
      return InitResult::Rejected;
    }
  }
  
  // Initialize OpenXR instance
  XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
  createInfo.enabledExtensionCount = requested_instance_extensions.size();
  vector<const char*> extension_name_pointers(requested_instance_extensions.size());
  for (usize i = 0; i < requested_instance_extensions.size(); ++ i) {
    extension_name_pointers[i] = requested_instance_extensions[i].data();
  }
  createInfo.enabledExtensionNames = extension_name_pointers.data();
  #ifdef _WIN32
  strncpy_s(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, applicationName, _TRUNCATE);
  #else
  strncpy(createInfo.applicationInfo.applicationName, applicationName, XR_MAX_APPLICATION_NAME_SIZE - 1);
  createInfo.applicationInfo.applicationName[XR_MAX_APPLICATION_NAME_SIZE - 1] = 0;
  #endif
  createInfo.applicationInfo.applicationVersion = 0;
  createInfo.applicationInfo.apiVersion = version;
  if (!XrCheckResult(xrCreateInstance(&createInfo, &instance)) || instance == XR_NULL_HANDLE) {
    LOG(ERROR) << "Failed to create an OpenXR instance. An OpenXR runtime must be installed and it must be active.";
    return OpenXRInstance::InitResult::InstanceUnavailable;
  }
  
  // Load extension function pointers
  functionTable = xrLoadInstanceFunctionTable(instance);
  
  // Set up debug printing
  if (enable_debug_layers) {
    XrDebugUtilsMessengerCreateInfoEXT debugInfo = {XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageTypes =
        XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
        XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
        XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
        XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debugInfo.messageSeverities =
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.userCallback = &DebugInfoUserCallback;
    if (functionTable.xrCreateDebugUtilsMessengerEXT) {
      if (!XrCheckResult(functionTable.xrCreateDebugUtilsMessengerEXT(instance, &debugInfo, &debugMessenger))) {
        LOG(ERROR) << "Failed to create a debug messenger";
      }
    }
  }
  
  // Get the instance properties for information
  XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
  if (XrCheckResult(xrGetInstanceProperties(instance, &instanceProperties))) {
    LOG(1) << "OpenXR runtime name: " << instanceProperties.runtimeName;
    LOG(1) << "OpenXR runtime version: " << instanceProperties.runtimeVersion;
  }
  
  // Request a form factor from the device (HMD, Handheld, etc.)
  XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  
  // Get the XR system
  XrResult getSystemResult = xrGetSystem(instance, &systemInfo, &systemId);
  if (getSystemResult == XR_SUCCESS) {
    // We successfully got a system.
  } else if (getSystemResult == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
    // The device(s) are not connected or not ready.
    return InitResult::FormFactorUnavailable;
  } else if (!XrCheckResult(getSystemResult)) {
    return InitResult::Error;
  }
  
  // Get the system properties for information
  XrSystemProperties systemProperties = {XR_TYPE_SYSTEM_PROPERTIES};
  if (XrCheckResult(xrGetSystemProperties(instance, systemId, &systemProperties))) {
    LOG(1) << "OpenXR system name: " << systemProperties.systemName;
    LOG(1) << "OpenXR vendor id: " << systemProperties.vendorId;
    
    LOG(1) << "OpenXR system maxSwapchainImageHeight: " << systemProperties.graphicsProperties.maxSwapchainImageHeight;
    LOG(1) << "OpenXR system maxSwapchainImageWidth: " << systemProperties.graphicsProperties.maxSwapchainImageWidth;
    LOG(1) << "OpenXR system maxLayerCount: " << systemProperties.graphicsProperties.maxLayerCount;
    
    LOG(1) << "OpenXR system orientationTracking: " << ((systemProperties.trackingProperties.orientationTracking == XR_TRUE) ? "yes" : "no");
    LOG(1) << "OpenXR system positionTracking: " << ((systemProperties.trackingProperties.positionTracking == XR_TRUE) ? "yes" : "no");
  }
  
  // Enumerate view configurations.
  // Note that the supported set of configurations must not change during the lifetime of the XrInstance according to the OpenXR specification,
  // so it makes sense to just enumerate them once and store them somewhere.
  u32 viewConfigurationCount;
  if (!XrCheckResult(xrEnumerateViewConfigurations(instance, systemId, 0, &viewConfigurationCount, nullptr))) {
    return InitResult::Error;
  }
  supportedViewConfigurations.resize(viewConfigurationCount);
  if (!XrCheckResult(xrEnumerateViewConfigurations(instance, systemId, viewConfigurationCount, &viewConfigurationCount, supportedViewConfigurations.data()))) {
    return InitResult::Error;
  }
  
  // Note: We could query xrGetViewConfigurationProperties() for each view configuration,
  //       but this currently only seems to specify whether the field-of-view of the type is mutable, which is of no interest to us.
  
  // Query the Vulkan API version requirement
  XrGraphicsRequirementsVulkanKHR graphicsRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  if (!XrCheckResult(functionTable.xrGetVulkanGraphicsRequirementsKHR(instance, systemId, &graphicsRequirements))) {
    return InitResult::Error;
  }
  minSupportedVulkanVersion = graphicsRequirements.minApiVersionSupported;
  LOG(1) << "Minimum supported Vulkan version of the OpenXR runtime: " << XR_VERSION_MAJOR(minSupportedVulkanVersion) << "." << XR_VERSION_MINOR(minSupportedVulkanVersion) << "." << XR_VERSION_PATCH(minSupportedVulkanVersion);
  maxSupportedVulkanVersion = graphicsRequirements.maxApiVersionSupported;
  LOG(1) << "Maximum supported Vulkan version of the OpenXR runtime: " << XR_VERSION_MAJOR(maxSupportedVulkanVersion) << "." << XR_VERSION_MINOR(maxSupportedVulkanVersion) << "." << XR_VERSION_PATCH(maxSupportedVulkanVersion);
  
  // Query the required Vulkan instance extensions (returned as a space-delimited string) and parse the returned string
  u32 extensionsStringSize;
  if (!XrCheckResult(functionTable.xrGetVulkanInstanceExtensionsKHR(instance, systemId, 0, &extensionsStringSize, nullptr))) {
    return InitResult::Error;
  }
  vector<char> extensionsString(extensionsStringSize);
  if (!XrCheckResult(functionTable.xrGetVulkanInstanceExtensionsKHR(instance, systemId, extensionsStringSize, &extensionsStringSize, extensionsString.data()))) {
    return InitResult::Error;
  }
  ParseSpaceDelimitedExtensionsString(extensionsString, &requiredVulkanInstanceExtensions);
  
  return InitResult::Success;
}

void OpenXRInstance::Destroy() {
  if (debugMessenger != XR_NULL_HANDLE && functionTable.xrDestroyDebugUtilsMessengerEXT) {
    functionTable.xrDestroyDebugUtilsMessengerEXT(debugMessenger);
    debugMessenger = XR_NULL_HANDLE;
  }
  if (instance != XR_NULL_HANDLE) {
    xrDestroyInstance(instance);
    instance = XR_NULL_HANDLE;
  }
}

bool OpenXRInstance::GetVulkanDeviceToUse(VkInstance vkInstance, VkPhysicalDevice* vkPhysicalDevice, vector<string>* vkDeviceExtensions) {
  // Query the VkPhysicalDevice that must be used
  if (!XrCheckResult(functionTable.xrGetVulkanGraphicsDeviceKHR(instance, systemId, vkInstance, vkPhysicalDevice))) {
    return false;
  }
  
  // Query the Vulkan device extensions that must be used
  u32 extensionsStringSize;
  if (!XrCheckResult(functionTable.xrGetVulkanDeviceExtensionsKHR(instance, systemId, 0, &extensionsStringSize, nullptr))) {
    return false;
  }
  vector<char> extensionsString(extensionsStringSize);
  if (!XrCheckResult(functionTable.xrGetVulkanDeviceExtensionsKHR(instance, systemId, extensionsStringSize, &extensionsStringSize, extensionsString.data()))) {
    return false;
  }
  ParseSpaceDelimitedExtensionsString(extensionsString, vkDeviceExtensions);
  
  return true;
}

vector<XrEnvironmentBlendMode> OpenXRInstance::GetSupportedBlendModes(XrViewConfigurationType viewConfigurationType) {
  u32 blendModeCount;
  if (!XrCheckResult(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, 0, &blendModeCount, nullptr))) {
    return {};
  }
  vector<XrEnvironmentBlendMode> supportedBlendModes(blendModeCount);
  if (!XrCheckResult(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, blendModeCount, &blendModeCount, supportedBlendModes.data()))) {
    return {};
  }
  
  return supportedBlendModes;
}

vector<XrViewConfigurationView> OpenXRInstance::EnumerateViews(XrViewConfigurationType viewConfigurationType) {
  u32 viewCount = 0;
  if (!XrCheckResult(xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, 0, &viewCount, nullptr))) {
    LOG(ERROR) << "xrEnumerateViewConfigurationViews() failed to return the count";
    return {};
  }
  vector<XrViewConfigurationView> xrConfigViews(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  if (!XrCheckResult(xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, viewCount, &viewCount, xrConfigViews.data()))) {
    LOG(ERROR) << "xrEnumerateViewConfigurationViews() failed to return the data (viewCount: " << viewCount << ")";
    return {};
  }
  return xrConfigViews;
}

XrPath OpenXRInstance::StringToPath(const char* str) {
  XrPath result;
  if (!XrCheckResult(xrStringToPath(instance, str, &result))) {
    LOG(ERROR) << "xrStringToPath() failed";
    return XR_NULL_PATH;
  }
  return result;
}

bool OpenXRInstance::SuggestInteractionProfileBindings(const char* profilePath, const vector<OpenXRSuggestedBinding>& bindings) {
  vector<XrActionSuggestedBinding> pathBindings(bindings.size());
  for (usize i = 0; i < bindings.size(); ++ i) {
    pathBindings[i].action = bindings[i].action;
    pathBindings[i].binding = StringToPath(bindings[i].bindingPath);
    if (pathBindings[i].binding == XR_NULL_PATH) {
      LOG(ERROR) << "Failed to convert string to OpenXR path: " << bindings[i].bindingPath;
      return false;
    }
  }
  
  XrInteractionProfileSuggestedBinding suggestedBinding = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggestedBinding.interactionProfile = StringToPath(profilePath);
  if (suggestedBinding.interactionProfile == XR_NULL_PATH) {
    LOG(ERROR) << "Failed to convert string to OpenXR path: " << profilePath;
    return false;
  }
  suggestedBinding.countSuggestedBindings = bindings.size();
  suggestedBinding.suggestedBindings = pathBindings.data();
  return XrCheckResult(xrSuggestInteractionProfileBindings(instance, &suggestedBinding));
}

bool OpenXRInstance::QueryAvailableInstanceExtensions(bool enable_debug_layers) {
  vector<string> requestedApiLayers;
  constexpr const char* coreValidationLayerName = "XR_APILAYER_LUNARG_core_validation";
  if (enable_debug_layers) {
    requestedApiLayers.push_back(coreValidationLayerName);
  }
  
  // List the available instance extensions, first general ones, then ones provided by enabled layers.
  availableInstanceExtensions.clear();
  
  uint32_t extensionCount = 0;
  if (!XrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr))) {
    LOG(ERROR) << "xrEnumerateInstanceExtensionProperties() failed";
    return false;
  }
  availableInstanceExtensions.resize(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  if (!XrCheckResult(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, availableInstanceExtensions.data()))) {
    LOG(ERROR) << "xrEnumerateInstanceExtensionProperties() failed";
    return false;
  }
  
  for (const auto& layer_name : requestedApiLayers) {
    u32 layer_extension_count = 0;
    xrEnumerateInstanceExtensionProperties(layer_name.data(), 0, &layer_extension_count, nullptr);
    usize oldExtensionCount = availableInstanceExtensions.size();
    availableInstanceExtensions.resize(availableInstanceExtensions.size() + layer_extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(layer_name.data(), layer_extension_count, &layer_extension_count, availableInstanceExtensions.data() + oldExtensionCount);
  }
  
  availableInstanceExtensionsQueried = true;
  availableInstanceExtensionsQueriedWithDebugLayers = enable_debug_layers;
  return true;
}

}

#endif
