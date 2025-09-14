#pragma once
#ifdef HAVE_OPENXR

// This file contains a simple mechanism to load OpenXR extension functions.
// TODO: Perhaps we could extend this by auto-generating it from the OpenXR specification,
//       analogously to how the "volk" loader for Vulkan works? Would there also be a similar
//       performance benefit to also loading the core functions in this way (as there is
//       for Vulkan)?

#include "scan_studio/viewer_common/openxr/openxr.hpp"

#define FOR_EACH_EXTENSION_FUNCTION(_) \
    _(xrCreateDebugUtilsMessengerEXT) \
    _(xrDestroyDebugUtilsMessengerEXT) \
    _(xrGetVulkanGraphicsRequirementsKHR) \
    _(xrGetVulkanInstanceExtensionsKHR) \
    _(xrGetVulkanGraphicsDeviceKHR) \
    _(xrGetVulkanDeviceExtensionsKHR) \
    /* XR_FB_passthrough */ \
    _(xrCreatePassthroughFB) \
	  _(xrDestroyPassthroughFB) \
	  _(xrPassthroughStartFB) \
	  _(xrPassthroughPauseFB) \
	  _(xrCreatePassthroughLayerFB) \
	  _(xrDestroyPassthroughLayerFB) \
	  _(xrPassthroughLayerPauseFB) \
	  _(xrPassthroughLayerResumeFB) \
	  _(xrPassthroughLayerSetStyleFB) \
	  _(xrCreateGeometryInstanceFB) \
	  _(xrDestroyGeometryInstanceFB) \
	  _(xrGeometryInstanceSetTransformFB)

#define GET_INSTANCE_PROC_ADDRESS(name) (void)xrGetInstanceProcAddr(instance, #name, (PFN_xrVoidFunction*)((PFN_##name*)(&result.name)));
#define DEFINE_PROC_MEMBER(name) PFN_##name name;

struct XrInstanceFunctionTable {
  FOR_EACH_EXTENSION_FUNCTION(DEFINE_PROC_MEMBER);
};

inline XrInstanceFunctionTable xrLoadInstanceFunctionTable(XrInstance instance) {
  XrInstanceFunctionTable result;
  FOR_EACH_EXTENSION_FUNCTION(GET_INSTANCE_PROC_ADDRESS);
  return result;
}

#undef DEFINE_PROC_MEMBER
#undef GET_INSTANCE_PROC_ADDRESS
#undef FOR_EACH_EXTENSION_FUNCTION

#endif
