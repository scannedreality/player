#ifdef HAVE_OPENXR

#pragma once

#include "scan_studio/viewer_common/openxr/openxr.hpp"
#include "scan_studio/viewer_common/openxr/loader.hpp"

namespace scan_studio {
using namespace vis;

struct OpenXRSuggestedBinding {
  XrAction action;
  const char* bindingPath;
};

class OpenXRInstance {
 public:
  /// Possible results of attempting to initialize an XR instance:
  enum class InitResult {
    /// Success
    Success = 0,
    
    /// The instance is unavailable, meaning that either no OpenXR runtime is installed, or it is not active.
    InstanceUnavailable,
    
    /// The form factor is unavailable, meaning that the runtime is available, but the XR device is not connected or not ready.
    FormFactorUnavailable,
    
    /// A requested extension or layer is not available in the OpenXR runtime.
    Rejected,
    
    /// An error occurred while trying to initialize.
    Error,
  };
  
  /// This does not initialize OpenXR yet. Initialize() must be called for that.
  OpenXRInstance();
  
  OpenXRInstance(const OpenXRInstance& other) = delete;
  OpenXRInstance& operator= (const OpenXRInstance& other) = delete;
  
  OpenXRInstance(OpenXRInstance&& other);
  OpenXRInstance& operator= (OpenXRInstance&& other);
  
  /// Destroys the instance.
  ~OpenXRInstance();
  
  /// Queries whether the given instance extension is supported.
  bool IsExtensionSupported(const char* extensionName, bool enable_debug_layers);
  
  /// Attempts to initialize the OpenXR instance.
  /// XR_MAKE_VERSION(major, minor, patch) may be used to create the version.
  /// If initialization fails because of a missing extension or layer (return value InitResult::Rejected) and
  /// missingExtensionOrLayer is non-null, then the name of the missing extension or layer is returned in missingExtensionOrLayer.
  InitResult Initialize(const char* applicationName, XrVersion version, const vector<string>& extensions, bool enable_debug_layers, string* missingExtensionOrLayer = nullptr);
  void Destroy();
  
  /// After Initialize() was called and a VkInstance allocated that conforms to the given requirements,
  /// GetVulkanDeviceToUse() must be called to obtain the Vulkan device that must be used for the XR session.
  /// Returns true if successful, false otherwise.
  bool GetVulkanDeviceToUse(VkInstance vkInstance, VkPhysicalDevice* vkPhysicalDevice, vector<string>* vkDeviceExtensions);
  
  /// Queries and returns the supported blend modes for the viewConfigurationType passed in.
  vector<XrEnvironmentBlendMode> GetSupportedBlendModes(XrViewConfigurationType viewConfigurationType);
  
  /// Enumerates the views, given the chosen view configuration type.
  vector<XrViewConfigurationView> EnumerateViews(XrViewConfigurationType viewConfigurationType);
  
  /// Returns the XrPath for the given path string, or XR_NULL_PATH in case of an error.
  /// Note that a valid path may be returned even though no object exists for the given path string.
  XrPath StringToPath(const char* str);
  
  /// Suggest an action mapping for an interaction profile (controller scheme).
  bool SuggestInteractionProfileBindings(const char* profilePath, const vector<OpenXRSuggestedBinding>& bindings);
  
  inline XrSystemId GetSystemId() const { return systemId; }
  
  inline const vector<XrViewConfigurationType>& GetSupportedViewConfigurations() const { return supportedViewConfigurations; }
  
  inline XrVersion GetMinSupportedVulkanVersion() const { return minSupportedVulkanVersion; }
  inline XrVersion GetMaxSupportedVulkanVersion() const { return maxSupportedVulkanVersion; }
  
  vector<string> GetRequiredVulkanInstanceExtensions() const { return requiredVulkanInstanceExtensions; }
  
  const XrInstanceFunctionTable& FunctionTable() const { return functionTable; }
  
  /// Returns the underlying XrInstance handle.
  inline const XrInstance& GetInstance() const { return instance; }
  inline operator XrInstance() const { return instance; }
  
 private:
  bool QueryAvailableInstanceExtensions(bool enable_debug_layers);
  
  XrInstance instance = XR_NULL_HANDLE;
  XrInstanceFunctionTable functionTable;
  
  XrSystemId systemId = XR_NULL_SYSTEM_ID;
  
  vector<XrViewConfigurationType> supportedViewConfigurations;
  
  bool availableInstanceExtensionsQueried = false;
  bool availableInstanceExtensionsQueriedWithDebugLayers;
  vector<XrExtensionProperties> availableInstanceExtensions;
  
  // Requirements on the used Vulkan instance
  XrVersion minSupportedVulkanVersion;
  XrVersion maxSupportedVulkanVersion;
  vector<string> requiredVulkanInstanceExtensions;
  
  XrDebugUtilsMessengerEXT debugMessenger = XR_NULL_HANDLE;
};

}

#endif
