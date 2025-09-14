#pragma once
#ifdef HAVE_OPENXR

#include <libvis/vulkan/forward_definitions.h>

#include "scan_studio/viewer_common/openxr/openxr.hpp"
#include "scan_studio/viewer_common/openxr/loader.hpp"

namespace scan_studio {
using namespace vis;

class OpenXRInstance;

class OpenXRSession {
 public:
  /// This does not initialize the object yet. Initialize() must be called for that.
  OpenXRSession();
  
  OpenXRSession(const OpenXRSession& other) = delete;
  OpenXRSession& operator= (const OpenXRSession& other) = delete;
  
  OpenXRSession(OpenXRSession&& other);
  OpenXRSession& operator= (OpenXRSession&& other);
  
  /// Destroys the session.
  ~OpenXRSession();
  
  /// Attempts to initialize the OpenXR session.
  /// Returns true if successful, false otherwise.
  bool Initialize(const OpenXRInstance& instance, u32 queueFamilyIndex, u32 queueIndex, const VulkanDevice& vkDevice, VkInstance vkInstance);
  void Destroy();
  
  bool AttachActionSets(const vector<XrActionSet>& actionSets);
  
  vector<s64> EnumerateSwapchainFormats();
  
  inline const vector<XrReferenceSpaceType>& GetSupportedReferenceSpaces() const { return supportedReferenceSpaces; }
  
  /// Returns the underlying XrSession handle.
  inline const XrSession& GetSession() const { return session; }
  inline operator XrSession() const { return session; }
  
 private:
  XrSession session = XR_NULL_HANDLE;
  
  vector<XrReferenceSpaceType> supportedReferenceSpaces;
};

}

#endif
