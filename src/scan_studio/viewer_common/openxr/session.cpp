#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/openxr/session.hpp"

#include <libvis/vulkan/device.h>

#include "scan_studio/viewer_common/openxr/instance.hpp"

namespace scan_studio {

OpenXRSession::OpenXRSession() {}

OpenXRSession::OpenXRSession(OpenXRSession&& other)
    : session(std::move(other.session)),
      supportedReferenceSpaces(std::move(other.supportedReferenceSpaces)) {
  other.session = XR_NULL_HANDLE;
}

OpenXRSession& OpenXRSession::operator=(OpenXRSession&& other) {
  Destroy();
  
  session = std::move(other.session);
  supportedReferenceSpaces = std::move(other.supportedReferenceSpaces);
  
  other.session = XR_NULL_HANDLE;
  return *this;
}

OpenXRSession::~OpenXRSession() {
  Destroy();
}

bool OpenXRSession::Initialize(const OpenXRInstance& instance, u32 queueFamilyIndex, u32 queueIndex, const VulkanDevice& vkDevice, VkInstance vkInstance) {
  XrGraphicsBindingVulkanKHR vulkanBinding = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  vulkanBinding.instance = vkInstance;
  vulkanBinding.physicalDevice = vkDevice.physical_device();
  vulkanBinding.device = vkDevice;
  vulkanBinding.queueFamilyIndex = queueFamilyIndex;
  vulkanBinding.queueIndex = queueIndex;
  
  XrSessionCreateInfo sessionInfo = {XR_TYPE_SESSION_CREATE_INFO};
  sessionInfo.next = &vulkanBinding;
  sessionInfo.systemId = instance.GetSystemId();
  
  if (!XrCheckResult(xrCreateSession(instance, &sessionInfo, &session)) || session == XR_NULL_HANDLE) {
    return false;
  }
  
  // Enumerate reference spaces (i.e., reference frames).
  // Note that according to the OpenXR specification of xrEnumerateReferenceSpaces(),
  // "Runtimes must always return identical buffer contents from this enumeration for the lifetime of the session".
  // Thus, it makes sense to just enumerate this once and store it somewhere.
  u32 supportedReferenceSpaceCount;
  if (!XrCheckResult(xrEnumerateReferenceSpaces(session, 0, &supportedReferenceSpaceCount, nullptr))) {
    return false;
  }
  supportedReferenceSpaces.resize(supportedReferenceSpaceCount);
  if (!XrCheckResult(xrEnumerateReferenceSpaces(session, supportedReferenceSpaceCount, &supportedReferenceSpaceCount, supportedReferenceSpaces.data()))) {
    return false;
  }
  
  return true;
}

void OpenXRSession::Destroy() {
  if (session != XR_NULL_HANDLE) {
    xrDestroySession(session);
    session = XR_NULL_HANDLE;
  }
}

bool OpenXRSession::AttachActionSets(const vector<XrActionSet>& actionSets) {
  XrSessionActionSetsAttachInfo attach_info = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach_info.countActionSets = actionSets.size();
  attach_info.actionSets = actionSets.data();
  return XrCheckResult(xrAttachSessionActionSets(session, &attach_info));
}

vector<s64> OpenXRSession::EnumerateSwapchainFormats() {
  u32 swapchainFormatCount;
  if (!XrCheckResult(xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, nullptr))) {
    return {};
  }
  vector<s64> swapchainFormats(swapchainFormatCount);
  if (!XrCheckResult(xrEnumerateSwapchainFormats(session, swapchainFormatCount, &swapchainFormatCount, swapchainFormats.data())) ||
      swapchainFormats.empty()) {
    return {};
  }
  
  return swapchainFormats;
}

}

#endif
