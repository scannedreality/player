#pragma once
#ifdef HAVE_OPENXR

#include <Eigen/Geometry>

#include "scan_studio/viewer_common/pi.hpp"
#include <sophus/se3.hpp>

#include <libvis/vulkan/gltf_renderer.h>
#include <libvis/vulkan/instance.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/openxr/action_set.hpp"
#include "scan_studio/viewer_common/openxr/instance.hpp"
#include "scan_studio/viewer_common/openxr/openxr_vulkan_application.hpp"
#include "scan_studio/viewer_common/openxr/session.hpp"
#include "scan_studio/viewer_common/openxr/space.hpp"
#include "scan_studio/viewer_common/openxr/swapchain.hpp"

#include "scan_studio/viewer_common/common.hpp"
#include "scan_studio/viewer_common/render_state.hpp"
#include "scan_studio/viewer_common/ui_xr.hpp"

namespace scan_studio {
using namespace vis;

class XRViewerScene;

class XRViewerApplication : public OpenXRApplicationCallbacks {
 friend class XRViewerScene;
 public:
  virtual bool SpecifyOpenXRInstanceExtensions(
      bool useOpenXRDebugLayers,
      vector<string>* extensions) override;
  virtual bool SpecifyVulkanDeviceFeaturesAndExtensions(
      const VulkanPhysicalDevice& vkPhysicalDevice,
      VkPhysicalDeviceFeatures* enabled_features,
      vector<string>* vkDeviceExtensions,
      const void** deviceCreateInfoPNext) override;
  virtual bool ChooseSpaceAndView(
      const vector<XrReferenceSpaceType>& supportedReferenceSpaceTypes,
      XrReferenceSpaceType* chosenReferenceSpaceType,
      const vector<XrViewConfigurationType>& supportedViewConfigurationTypes,
      XrViewConfigurationType* chosenViewConfigurationType) override;
  virtual void ChooseMsaaSamples(const VulkanPhysicalDevice &vkPhysicalDevice, VkSampleCountFlagBits *msaaSamples) override;
  virtual bool Initialize() override;
  
  virtual void OnCurrentInteractionProfilesChanged() override;
  
  virtual void OnError(const string& message) override;
  
 private:
  // OpenXR extensions
  bool usingExtension_XR_EXT_hp_mixed_reality_controller = false;
  
  // Actions
  OpenXRActionSet actionSet;
  vector<XrPath> handSubactionPaths;
  XrAction handPoseAction;
  XrAction gripAction;
  XrAction selectAction;
  XrAction rotateViewAction;
  XrAction rotateViewDirectionAction;
  XrSpace handPoseSpace[2];
  
  /// Whether the controller in the given hand uses single-button input.
  /// If true, the gripAction controls both camera movement (click-and-drag) and selection (single-click).
  /// If false, the gripAction only controls camera movement, while the separate selectAction is used for selection.
  bool useSingleButtonInputScheme[2];  // for left, right hand
};

class XRViewerScene : public OpenXRSceneCallbacks {
 public:
  XRViewerScene(const char* videoPath, bool cacheAllFrames);
  
  bool Initialize(bool verboseDecoding);
  
  virtual void OnEnterScene() override;
  virtual void OnLeaveScene() override;
  
  virtual void PollActions(XrTime predictedDisplayTime) override;
  
  virtual void PrepareFrame(VulkanCommandBuffer* cmdBuf, XrTime predictedDisplayTime, float* nearZ, float* farZ) override;
  virtual void RenderView(VulkanCommandBuffer* cmdBuf, u32 viewIndex, XrTime predictedDisplayTime, const VulkanFramebuffer& framebuffer, u32 width, u32 height) override;
  virtual void SetRenderMatrices(XrTime predictedDisplayTime, XrViewStateFlags viewStateFlags) override;
  
 private:
  // Actions
  bool handIsBeingTracked[2] = {false, false};
  Sophus::SE3f last_xrspace_tr_predictedHandPose[2];
  
  // State for implementing the single-button input scheme:
  Sophus::SE3f gripStart_xrspace_tr_eventPose[2];
  XrTime gripStartTime[2];
  static constexpr float clickMoveThreshold = 0.02;
  bool clickMoveThresholdExceeded[2] = {false, false};
  
  /// Last predicted pose of the grip hands, for grip handling.
  Sophus::SE3f xrspace_tr_lastPredictedGriphandPose[2];
  bool gripActive[2] = {false, false};
  
  /// The current pose of the XRVideo ("model transformation" in computer graphics terms).
  Sophus::SE3f xrspace_tr_xrvideo;
  float currentXRVideoYaw = 0;
  
  static constexpr float zNear = 0.05f;
  static constexpr float zFar = 20.f;
  
  // FontStash
  FontStashShader fontstashShader;
  unique_ptr<FontStash> fontstash;
  int fontstashFont;
  
  // Shape2D
  Shape2DShader shape2DShader;
  
  // XR-specific content
  VulkanGLTFRendererCommonResources gltfCommonResources;
  VulkanGLTFRendererEnvironment gltfEnvironment;
  
  VulkanGLTFRenderer gltfHandCursors[2];
  
  // Application logic (common to desktop and XR render path)
  const char* videoPath;
  XrTime startTime = -1;
  VulkanRenderState vulkanRenderState;
  XRUI xrUI;
  ViewerCommon commonLogic;
};

bool ShowXRViewer(bool cacheAllFrames, const char* videoPath, bool verboseDecoding, bool useVulkanDebugLayers, bool useOpenXRDebugLayers);

}

#endif
