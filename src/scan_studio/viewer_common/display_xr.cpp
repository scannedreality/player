#ifdef HAVE_OPENXR

#include "scan_studio/viewer_common/display_xr.hpp"

#include <array>
#include <thread>

#include <libvis/io/filesystem.h>

#ifndef __ANDROID__
  #include <SDL.h>
#endif

#include <Eigen/Geometry>

#include <libvis/io/globals.h>

#include <libvis/vulkan/physical_device.h>
#include <libvis/vulkan/queue.h>
#include <libvis/vulkan/transform_matrices.h>

#include "scan_studio/common/xrvideo_file.hpp"
#include "scan_studio/common/sRGB.hpp"

#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

namespace scan_studio {

bool XRViewerApplication::SpecifyOpenXRInstanceExtensions(
    bool useOpenXRDebugLayers,
    vector<string>* extensions) {
  if (app->XRInstance().IsExtensionSupported(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME, useOpenXRDebugLayers)) {
    usingExtension_XR_EXT_hp_mixed_reality_controller = true;
    extensions->push_back(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
  }
  return true;
}

bool XRViewerApplication::SpecifyVulkanDeviceFeaturesAndExtensions(
    const VulkanPhysicalDevice& vkPhysicalDevice,
    VkPhysicalDeviceFeatures* /*enabled_features*/,
    vector<string>* /*vkDeviceExtensions*/,
    const void** /*deviceCreateInfoPNext*/) {
  const int selected_transfer_queue_family_index = vkPhysicalDevice.FindSeparateTransferQueueFamily();
  
  app->VKDevice().RequestQueues(selected_transfer_queue_family_index, /*count*/ 1);
  app->VKDevice().SetTransferQueueFamilyIndex(selected_transfer_queue_family_index);
  
  return true;
}

bool XRViewerApplication::ChooseSpaceAndView(
    const vector<XrReferenceSpaceType>& supportedReferenceSpaceTypes,
    XrReferenceSpaceType* chosenReferenceSpaceType,
    const vector<XrViewConfigurationType>& supportedViewConfigurationTypes,
    XrViewConfigurationType* chosenViewConfigurationType) {
  constexpr XrReferenceSpaceType preferredRefSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  *chosenReferenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;  // must be supported by all runtimes
  for (auto spaceType : supportedReferenceSpaceTypes) {
    if (spaceType == preferredRefSpaceType) {
      *chosenReferenceSpaceType = preferredRefSpaceType;
      break;
    }
  }
  
  bool viewConfigurationTypeFound = false;
  for (const XrViewConfigurationType& type : supportedViewConfigurationTypes) {
    if (type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
      *chosenViewConfigurationType = type;
      viewConfigurationTypeFound = true;
      break;
    }
  }
  
  if (!viewConfigurationTypeFound) {
    // TODO: Make the application run with mono view mode as well (for smartphone AR)
    const string errorMsg = "Did not find a suitable supported OpenXR view configuration type. The application currently requires stereo view mode";
    LOG(ERROR) << errorMsg;
    OnError(errorMsg.c_str());
    return false;
  }
  
  return true;
}

void XRViewerApplication::ChooseMsaaSamples(const vis::VulkanPhysicalDevice& vkPhysicalDevice, VkSampleCountFlagBits* msaaSamples) {
  *msaaSamples = vkPhysicalDevice.QueryMaxUsableSampleCount();
}

bool XRViewerApplication::Initialize() {
  // Create xrvideo_viewing action set
  if (!actionSet.Initialize("xrvideo_viewing", "XRVideo Viewing", app->XRInstance())) {
    LOG(ERROR) << "xrCreateActionSet() failed";
    OnError("xrCreateActionSet() failed.");
    return false;
  }
  
  // Get subaction paths for the left and right hand
  handSubactionPaths.resize(2);
  handSubactionPaths[0] = app->XRInstance().StringToPath("/user/hand/left");
  handSubactionPaths[1] = app->XRInstance().StringToPath("/user/hand/right");
  if (handSubactionPaths[0] == XR_NULL_PATH || handSubactionPaths[1] == XR_NULL_PATH) {
    LOG(ERROR) << "xrStringToPath() failed";
    OnError("xrStringToPath() failed.");
    return false;
  }
  
  // Create actions such that they can be used by both the left and the right hand
  handPoseAction = actionSet.CreateAction(XR_ACTION_TYPE_POSE_INPUT, "hand_pose", "Hand Pose", &handSubactionPaths);
  gripAction = actionSet.CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "grip", "Grip", &handSubactionPaths);
  selectAction = actionSet.CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "select", "Select", &handSubactionPaths);
  rotateViewAction = actionSet.CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "rotate_view", "Rotate the view", &handSubactionPaths);
  rotateViewDirectionAction = actionSet.CreateAction(XR_ACTION_TYPE_FLOAT_INPUT, "rotate_view_direction", "Set the direction of view rotation", &handSubactionPaths);
  
  if (handPoseAction == XR_NULL_HANDLE ||
      gripAction == XR_NULL_HANDLE ||
      rotateViewAction == XR_NULL_HANDLE ||
      rotateViewDirectionAction == XR_NULL_HANDLE) {
    LOG(ERROR) << "Failed to create an OpenXR action";
    OnError("Failed to create an OpenXR action.");
    return false;
  }
  
  // Bind the actions to specific locations on the Khronos simple_controller definition.
  // NOTE: We do not bind the rotate left/right actions here, since there are no suitable locations for them on this controller scheme.
  app->XRInstance().SuggestInteractionProfileBindings(
      "/interaction_profiles/khr/simple_controller",
      {{handPoseAction, "/user/hand/left/input/aim/pose"},
       {handPoseAction, "/user/hand/right/input/aim/pose"},
       {gripAction, "/user/hand/left/input/select/click"},
       {gripAction, "/user/hand/right/input/select/click"}});
  
  // Bind the actions to specific locations on the HTC Vive "wand" controller.
  app->XRInstance().SuggestInteractionProfileBindings(
      "/interaction_profiles/htc/vive_controller",
      {{handPoseAction, "/user/hand/left/input/aim/pose"},
       {handPoseAction, "/user/hand/right/input/aim/pose"},
       {gripAction, "/user/hand/left/input/squeeze/click"},
       {gripAction, "/user/hand/right/input/squeeze/click"},
       {selectAction, "/user/hand/left/input/trigger/click"},
       {selectAction, "/user/hand/right/input/trigger/click"},
       {rotateViewAction, "/user/hand/left/input/trackpad/click"},
       {rotateViewAction, "/user/hand/right/input/trackpad/click"},
       {rotateViewDirectionAction, "/user/hand/left/input/trackpad/x"},
       {rotateViewDirectionAction, "/user/hand/right/input/trackpad/x"}});
  
  // Bind the actions to specific locations on the WMR controller.
  if (usingExtension_XR_EXT_hp_mixed_reality_controller) {
    // TODO: Bind the rotate-view action such that it gets activated when moving the thumbstick to the side fully (but don't require a click action, as for the Vive trackpad)
    app->XRInstance().SuggestInteractionProfileBindings(
        "/interaction_profiles/hp/mixed_reality_controller",
        {{handPoseAction, "/user/hand/left/input/aim/pose"},
        {handPoseAction, "/user/hand/right/input/aim/pose"},
        {gripAction, "/user/hand/left/input/squeeze"},
        {gripAction, "/user/hand/right/input/squeeze"},
        {selectAction, "/user/hand/left/input/trigger"},
        {selectAction, "/user/hand/right/input/trigger"}});
  }
  
  // TODO: Add more controller bindings
  
  // Create frames of reference for the pose actions
  // Note: Those are deleted automatically when the parent action gets deleted.
  for (int i = 0; i < 2; ++ i) {
    XrActionSpaceCreateInfo action_space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    action_space_info.action = handPoseAction;
    action_space_info.subactionPath = handSubactionPaths[i];
    action_space_info.poseInActionSpace = xrIdentityPose;
    if (!XrCheckResult(xrCreateActionSpace(app->XRSession(), &action_space_info, &handPoseSpace[i]))) {
      LOG(ERROR) << "xrCreateActionSpace() failed";
      OnError("xrCreateActionSpace() failed");
      return false;
    }
  }
  
  // Attach the action sets to the session
  if (!app->XRSession().AttachActionSets({actionSet})) {
    LOG(ERROR) << "xrSession.AttachActionSets() failed";
    OnError("xrSession.AttachActionSets() failed");
    return false;
  }
  
  return true;
}

void XRViewerApplication::OnCurrentInteractionProfilesChanged() {
  /// Queries which interaction profiles are being used.
  /// This is useful since some controller schemes offer only few buttons, which is not enough
  // to map all of our actions to them separately. Thus, we have to check for the use of such
  // controller schemes and implicitly map multiple actions to the same buttons in these cases
  // (for example: short pressing a button may cause action A, while pressing the button
  //  and moving the controller may cause action B).
  
  // Define the controller schemes for which we want to use single-button input:
  const vector<string> singleButtonInputInteractionProfileStrings = {
      "/interaction_profiles/khr/simple_controller"};
  
  // Convert string to XrPath
  vector<XrPath> singleButtonInputInteractionProfilePaths;
  for (const string& profileString : singleButtonInputInteractionProfileStrings) {
    XrPath path = app->XRInstance().StringToPath(profileString.c_str());
    if (path != XR_NULL_PATH) {
      singleButtonInputInteractionProfilePaths.emplace_back(path);
    }
  }
  
  // Query the interaction scheme for each hand and compare it to those listed above.
  // Set useSingleButtonInputScheme[hand] accordingly.
  for (int hand = 0; hand < 2; ++ hand) {
    XrInteractionProfileState interactionProfile{XR_TYPE_INTERACTION_PROFILE_STATE};
    
    if (XrCheckResult(xrGetCurrentInteractionProfile(app->XRSession(), handSubactionPaths[hand], &interactionProfile))) {
      constexpr int interactionProfilePathSize = 256;
      char interactionProfilePath[interactionProfilePathSize];
      u32 interactionProfilePathLen;
      string interactionProfilePathString;
      if (xrPathToString(app->XRInstance(), interactionProfile.interactionProfile, interactionProfilePathSize, &interactionProfilePathLen, interactionProfilePath) == XR_SUCCESS) {
        interactionProfilePathString = interactionProfilePath;
      } else if (interactionProfile.interactionProfile == XR_NULL_PATH) {
        interactionProfilePathString = "none";
      } else {
        interactionProfilePathString = "unknown";
      }
      LOG(1) << "Interaction profile used for " << ((hand == 0) ? "left" : "right") << " hand: " << interactionProfilePathString;
      
      useSingleButtonInputScheme[hand] = false;
      for (const XrPath& profilePath : singleButtonInputInteractionProfilePaths) {
        if (profilePath == interactionProfile.interactionProfile) {
          useSingleButtonInputScheme[hand] = true;
          break;
        }
      }
    } else {
      LOG(ERROR) << "xrGetCurrentInteractionProfile() failed";
      useSingleButtonInputScheme[hand] = true;
    }
  }
}

void XRViewerApplication::OnError(const string& message) {
  #ifdef __ANDROID__
    (void) message;
  #else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", ("Error: " + message).c_str(), nullptr);
  #endif
}


XRViewerScene::XRViewerScene(const char* videoPath, bool cacheAllFrames)
    : videoPath(videoPath),
      commonLogic(RendererType::Vulkan_1_0_OpenXR_1_0, cacheAllFrames) {}

bool XRViewerScene::Initialize(bool verboseDecoding) {
  // Initialize common application logic
  commonLogic.Initialize();
  #ifdef HAVE_VULKAN
  // TODO: There is a prior call to appCallbacks->ChooseMsaaSamples() in OpenXRVulkanApplication.
  //       It would be cleaner to use that call's result, instead of calling ChooseMsaaSamples() a second time here.
  VkSampleCountFlagBits msaaSamples;
  appCallbacks->ChooseMsaaSamples(app->VKDevice().physical_device(), &msaaSamples);
  commonLogic.InitializeVulkan(app->MaxFrameInFlightCount(), msaaSamples, app->RenderPass(), &app->VKDevice());
  #endif
  if (!commonLogic.InitializeGraphicObjects(app->XRViews().size(), verboseDecoding, /*openGLContextCreationUserPtr*/ nullptr)) {
    LOG(ERROR) << "InitializeGraphicObjects() failed";
    #ifndef __ANDROID__
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to load graphics objects.", nullptr);
    #endif
    return false;
  }
  if (!commonLogic.OpenFile(/*preReadCompleteFile*/ false, videoPath)) {
    LOG(ERROR) << "OpenFile() failed";
#ifndef __ANDROID__
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to load video file.", nullptr);
#endif
    return false;
  }
  
  VulkanRenderState vulkanRenderState(
      &app->VKDevice(),
      &app->RenderPass(),
      app->MaxFrameInFlightCount(),
      /*cmdBuf*/ nullptr,
      /*currentFrameInFlightIndex*/ 0);
  
  // Initialize FontStash
  if (!fontstashShader.Initialize(/*enableDepthTesting*/ true, &vulkanRenderState)) { return false; }
  fontstash.reset(FontStash::Create(/*textureWidth*/ 2048, /*textureHeight*/ 1024, &fontstashShader, &vulkanRenderState));
  if (!fontstash) { return false; }
  
  fontstashFont = fontstash->LoadFont("DroidSans", OpenAssetUnique(fs::path("resources") / "fonts" / "droid-sans" / "DroidSans.ttf", /*isRelativeToAppPath*/ true));
  if (fontstashFont == FONS_INVALID) { LOG(ERROR) << "Failed to load DroidSans.ttf"; return false; }
  
  // Configure FontStash for 3D text rendering
  constexpr float metersPerPixel = 0.0005f;  // 0.5mm per pixel
  fonsSetRoundingAndScaling(fontstash->GetContext(), /*roundToInt*/ false, /*scaling*/ metersPerPixel);
  
  // Initialize Shape2D shader
  if (!shape2DShader.Initialize(/*enableDepthTesting*/ true, &vulkanRenderState)) { return false; }
  
  // Initialize VR-specific application logic
  if (!xrUI.Initialize(commonLogic.GetXRVideo().get(), commonLogic.GetAudio().get(), app->XRViews().size(), &shape2DShader, fontstash.get(), fontstashFont, metersPerPixel, &vulkanRenderState)) {
    LOG(ERROR) << "xrUI.Initialize() failed";
    #ifndef __ANDROID__
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to initialize UI.", nullptr);
    #endif
    return false;
  }
  
  if (!gltfCommonResources.Initialize(
      AssetPath::CreateUnique(fs::path("resources") / "gltf_renderer", /*isRelativeToAppPath*/ true),
      &app->GraphicsCommandPool(),
      app->VKDevice().GetQueue(app->VKDevice().graphics_queue_family_index(), /*queue_index*/ 0),
      app->RenderPass(),
      &app->VKDevice())) {
    LOG(ERROR) << "gltfCommonResources.Initialize() failed";
    #ifndef __ANDROID__
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to initialize glTF common resources.", nullptr);
    #endif
    return false;
  }
  
  if (!gltfEnvironment.Initialize(
      OpenAssetUnique(fs::path("resources") / "environments" / "cayley_interior.ktx", /*isRelativeToAppPath*/ true),
      &gltfCommonResources)) {
    LOG(ERROR) << "gltfEnvironment.Initialize() failed";
    #ifndef __ANDROID__
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to initialize glTF environment.", nullptr);
    #endif
    return false;
  }
  
  for (int hand = 0; hand < 2; ++ hand) {
    // TODO: We currently load the same model twice. If we continue to use the same model for the left and right hand, we only need to load it once.
    if (!gltfHandCursors[hand].Initialize(
        MaybePrependAppPath(fs::path("resources") / "models" / "vr_hand_cursor.glb", /*isRelativeToAppPath*/ true),
        app->MaxFrameInFlightCount(),
        app->XRViews().size(),
        &gltfEnvironment,
        &app->GraphicsCommandPool(),  // graphics pool used for transfer
        app->VKDevice().GetQueue(app->VKDevice().graphics_queue_family_index(), /*queue_index*/ 0))) {  // graphics queue used for transfer
      LOG(ERROR) << "gltfHandCursors[hand].Initialize() failed";
      #ifndef __ANDROID__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to load a glTF hand cursor model.", nullptr);
      #endif
      return false;
    }
  }
  
  return true;
}

void XRViewerScene::OnEnterScene() {
  // If the XRVideo has view metadata, initialize the XRVideo pose relative to the user with it.
  // TODO
}

void XRViewerScene::OnLeaveScene() {
  // Nothing yet
}

void XRViewerScene::PollActions(XrTime predictedDisplayTime) {
  XRViewerApplication* appC = reinterpret_cast<XRViewerApplication*>(appCallbacks);
  
  bool selectPressed[2];
  
  // Update the action set with up-to-date input data
  XrActiveActionSet activeActionSet = {};
  activeActionSet.actionSet = appC->actionSet;
  activeActionSet.subactionPath = XR_NULL_PATH;  // use all subaction paths on the actions in the action set
  
  XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
  syncInfo.countActiveActionSets = 1;
  syncInfo.activeActionSets = &activeActionSet;
  if (!XrCheckResult(xrSyncActions(app->XRSession(), &syncInfo))) { LOG(ERROR) << "xrSyncActions() failed"; return; }
  
  // Get the current states of our actions and store them for later use
  for (int hand = 0; hand < 2; ++ hand) {
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.subactionPath = appC->handSubactionPaths[hand];
    
    // Check whether the hand/controller is being tracked
    XrActionStatePose poseState = {XR_TYPE_ACTION_STATE_POSE};
    getInfo.action = appC->handPoseAction;
    if (!XrCheckResult(xrGetActionStatePose(app->XRSession(), &getInfo, &poseState))) { LOG(ERROR) << "xrGetActionStatePose() failed"; return; }
    handIsBeingTracked[hand] = poseState.isActive;
    
    if (!handIsBeingTracked[hand]) {
      gripActive[hand] = false;
    }
    
    // Query selectAction
    XrActionStateBoolean selectState = {XR_TYPE_ACTION_STATE_BOOLEAN};
    getInfo.action = appC->selectAction;
    if (!XrCheckResult(xrGetActionStateBoolean(app->XRSession(), &getInfo, &selectState))) { LOG(ERROR) << "xrGetActionStateBoolean() failed"; return; }
    
    if (selectState.isActive == XR_TRUE && selectState.changedSinceLastSync) {
      Sophus::SE3f xrspace_tr_hand_at_event;
      Sophus::SE3f xrspace_tr_head_at_event;
      
      if (GetEigenPose(app->XRSpace(), appC->handPoseSpace[hand], selectState.lastChangeTime, &xrspace_tr_hand_at_event) &&
          GetEigenPose(app->XRSpace(), app->XRViewSpace(), selectState.lastChangeTime, &xrspace_tr_head_at_event)) {
        if (selectState.currentState == XR_TRUE) {
          xrUI.ClickPress(hand, xrspace_tr_hand_at_event, xrspace_tr_head_at_event);
        } else {
          xrUI.ClickRelease(hand, xrspace_tr_hand_at_event);
        }
      }
    }
    
    selectPressed[hand] = selectState.isActive == XR_TRUE && selectState.currentState == XR_TRUE;
    
    // Query rotateViewAction
    XrActionStateBoolean rotateViewState = {XR_TYPE_ACTION_STATE_BOOLEAN};
    getInfo.action = appC->rotateViewAction;
    if (!XrCheckResult(xrGetActionStateBoolean(app->XRSession(), &getInfo, &rotateViewState))) { LOG(ERROR) << "xrGetActionStateBoolean() failed"; return; }
    
    if (rotateViewState.isActive == XR_TRUE && rotateViewState.currentState == XR_TRUE && rotateViewState.changedSinceLastSync == XR_TRUE) {
      // Query rotateViewDirectionAction for the direction of the rotation
      XrActionStateFloat rotateViewDirectionState = {XR_TYPE_ACTION_STATE_FLOAT};
      getInfo.action = appC->rotateViewDirectionAction;
      if (!XrCheckResult(xrGetActionStateFloat(app->XRSession(), &getInfo, &rotateViewDirectionState))) { LOG(ERROR) << "xrGetActionStateFloat() failed"; return; }
      
      if (rotateViewDirectionState.isActive == XR_TRUE) {
        constexpr float rotationThreshold = 0.3f;
        if (fabs(rotateViewDirectionState.currentState) > rotationThreshold) {
          // Get the view (head) pose
          Sophus::SE3f xrspace_tr_head;
          if (GetEigenPose(app->XRSpace(), app->XRViewSpace(), rotateViewState.lastChangeTime, &xrspace_tr_head)) {
            // Rotate the xrspace around the head position
            constexpr float kRotationAngleStep = 20.f / 180.f * M_PI;
            float signedAngleStep = kRotationAngleStep * ((rotateViewDirectionState.currentState > 0) ? -1 : 1);
            
            Sophus::SE3f headpos_tr_xrspace(Eigen::Quaternionf::Identity(), -1 * xrspace_tr_head.translation());
            Sophus::SE3f rotation(Eigen::Quaternionf(Eigen::AngleAxisf(signedAngleStep, Eigen::Vector3f(0, 1, 0))), Eigen::Vector3f::Zero());
            Sophus::SE3f xrspace_tr_headpos(Eigen::Quaternionf::Identity(), xrspace_tr_head.translation());
            
            xrspace_tr_xrvideo = xrspace_tr_headpos * rotation * headpos_tr_xrspace * xrspace_tr_xrvideo;
            currentXRVideoYaw += signedAngleStep;
          }
        }
      }
    }
    
    // Query gripAction
    XrActionStateBoolean gripState = {XR_TYPE_ACTION_STATE_BOOLEAN};
    getInfo.action = appC->gripAction;
    if (!XrCheckResult(xrGetActionStateBoolean(app->XRSession(), &getInfo, &gripState))) { LOG(ERROR) << "xrGetActionStateBoolean() failed"; return; }
    
    if (gripState.isActive == XR_TRUE && handIsBeingTracked[hand]) {
      const bool gripActionChangedState = gripState.changedSinceLastSync == XR_TRUE;
      const bool gripActionIsActive = gripState.currentState == XR_TRUE;
      
      bool have_xrspace_tr_eventPose = false;
      Sophus::SE3f xrspace_tr_eventPose;
      if (gripActionChangedState) {
        // Get the hand pose at the event's timestamp.
        have_xrspace_tr_eventPose = GetEigenPose(app->XRSpace(), appC->handPoseSpace[hand], gripState.lastChangeTime, &xrspace_tr_eventPose);
      }
      
      bool have_xrspace_tr_predictedPose = false;
      Sophus::SE3f xrspace_tr_predictedPose;
      if (gripActionIsActive) {
        // Get the hand pose at the predicted display time.
        have_xrspace_tr_predictedPose = GetEigenPose(app->XRSpace(), appC->handPoseSpace[hand], predictedDisplayTime, &xrspace_tr_predictedPose);
      }
      
      Sophus::SE3f* xrspace_tr_new = nullptr;
      Sophus::SE3f* xrspace_tr_old = nullptr;
      
      // Check for clickMoveThresholdExceeded
      if (gripActive[hand] && have_xrspace_tr_predictedPose && !clickMoveThresholdExceeded[hand]) {
        const float movementDistance = (gripStart_xrspace_tr_eventPose[hand].translation() - xrspace_tr_predictedPose.translation()).norm();
        clickMoveThresholdExceeded[hand] = movementDistance > clickMoveThreshold;
      }
      
      if (gripActionChangedState && gripActionIsActive) {
        // Apply delta from grip start pose to current predicted pose
        if (have_xrspace_tr_eventPose && have_xrspace_tr_predictedPose) {
          xrspace_tr_new = &xrspace_tr_predictedPose;
          xrspace_tr_old = &xrspace_tr_eventPose;
          gripActive[hand] = true;
          
          // Initialize the state for detecting clicks in the single-button input scheme
          gripStart_xrspace_tr_eventPose[hand] = xrspace_tr_eventPose;
          gripStartTime[hand] = gripState.lastChangeTime;
          clickMoveThresholdExceeded[hand] = false;
        }
      } else if (gripActionChangedState && !gripActionIsActive) {
        // In principle: Apply delta from last predicted pose to grip end pose.
        // However, due to the application rendering frames with predicted poses in advance, this leads to the pose
        // "jumping back" when the controller is in motion while releasing the trigger, which looks bad.
        // Thus, we leave the pose at the last predicted pose here, which avoids the jump and thus looks significantly better,
        // even though the overall motion less accurately reflects the overall controller motion then
        // (but that does not matter in our case, since the application is not a game that requires precision).
        
        // if (gripActive[hand] && have_xrspace_tr_eventPose) {
        //   xrspace_tr_new = &xrspace_tr_eventPose;
        //   xrspace_tr_old = &xrspace_tr_lastPredictedGriphandPose[hand];
        // }
        
        if (appC->useSingleButtonInputScheme[hand] && gripActive[hand] && have_xrspace_tr_eventPose) {
          // Determine whether this was a "click" instead of a "drag" action:
          // * The press time of the action was short
          // * The controller never moved far away from its initial position
          // If so, we interpret the action as a click.
          constexpr s64 gripTimeThresholdNanoseconds = 0.75 * 1000 * 1000 * 1000;
          const s64 gripNanoseconds = gripState.lastChangeTime - gripStartTime[hand];
          
          if (gripNanoseconds < gripTimeThresholdNanoseconds &&
              !clickMoveThresholdExceeded[hand]) {
            Sophus::SE3f xrspace_tr_head_at_event;
            if (GetEigenPose(app->XRSpace(), app->XRViewSpace(), gripState.lastChangeTime, &xrspace_tr_head_at_event)) {
              xrUI.ClickPress(hand, xrspace_tr_eventPose, xrspace_tr_head_at_event);
              // TODO: Add ability to do "dragging" using single-button input instead of always releasing immediately.
              //       For example, we could determine whether the single button is pressed on a UI element.
              //       In this case, we simulate the click press immediately, such that we can then handle dragging.
              xrUI.ClickRelease(hand, xrspace_tr_eventPose);
            }
          }
        }
        
        gripActive[hand] = false;
      } else if (gripActionIsActive) {
        // Apply delta from last predicted pose to current predicted pose
        if (gripActive[hand] && have_xrspace_tr_predictedPose) {
          xrspace_tr_new = &xrspace_tr_predictedPose;
          xrspace_tr_old = &xrspace_tr_lastPredictedGriphandPose[hand];
        }
      } else {
        gripActive[hand] = false;
      }
      
      if (xrspace_tr_old && xrspace_tr_new) {
        // For unconstrained 6DOF movement, one would use:
        // xrspace_tr_xrvideo = *xrspace_tr_new * xrspace_tr_old->inverse() * xrspace_tr_xrvideo;
        
        // For 4DOF movement, where the rotation is constrained to go around the y axis only:
        // NOTE: There is surely a much more elegant way to compute this rotation angle ...?
        Eigen::Vector3f originalForwardDir(0, 0, 1);
        Eigen::Vector3f rotatedForwardDir = xrspace_tr_new->unit_quaternion() * xrspace_tr_old->unit_quaternion().inverse() * originalForwardDir;
        const float oldAngle = atan2f(originalForwardDir.x(), originalForwardDir.z());
        const float newAngle = atan2f(rotatedForwardDir.x(), rotatedForwardDir.z());
        const float yawChange = newAngle - oldAngle;
        if (!isnan(yawChange) && !isinf(yawChange)) {
          currentXRVideoYaw += yawChange;
        }
        
        const Eigen::Vector3f& oldTranslation = xrspace_tr_old->translation();
        const Eigen::Vector3f& newTranslation = xrspace_tr_new->translation();
        
        xrspace_tr_xrvideo = Sophus::SE3f(
            Eigen::Quaternionf(Eigen::AngleAxisf(currentXRVideoYaw, Eigen::Vector3f(0, 1, 0))),
            xrspace_tr_xrvideo.translation() + newTranslation - oldTranslation);
      }
      
      if (gripActionIsActive && have_xrspace_tr_predictedPose) {
        xrspace_tr_lastPredictedGriphandPose[hand] = xrspace_tr_predictedPose;
      }
    }
  }
  
  // Update hovering for xrUI
  Sophus::SE3f xrspace_tr_hand[2];
  bool handActive[2];
  for (int hand = 0; hand < 2; ++ hand) {
    handActive[hand] = GetEigenPose(app->XRSpace(), appC->handPoseSpace[hand], predictedDisplayTime, &xrspace_tr_hand[hand]);
  }
  xrUI.Hover(xrspace_tr_hand, handActive, selectPressed);
}

void XRViewerScene::PrepareFrame(VulkanCommandBuffer* cmdBuf, XrTime predictedDisplayTime, float* nearZ, float* farZ) {
  *nearZ = zNear;
  *farZ = zFar;
  
  if (startTime < 0) {
    // We use half a frame offset for the start time to reduce the change of getting
    // a bad alignment between the video and display frames that may lead to stuttering.
    startTime = predictedDisplayTime - static_cast<XrTime>((0.5 / 30) * 1e9);
  }
  
  // Create the render state (since some objects keep a pointer to it, it is important that this stays valid
  // over the PrepareFrame(), ..., RenderView() calls).
  vulkanRenderState = VulkanRenderState(
      &app->VKDevice(),
      &app->RenderPass(),
      app->MaxFrameInFlightCount(),
      cmdBuf,
      app->CurrentFrameInFlightIndex());
  
  const s64 predictedDisplayTimeNanoseconds = predictedDisplayTime - startTime;
  
  commonLogic.PrepareFrame(predictedDisplayTimeNanoseconds, xrUI.IsPaused(), &vulkanRenderState);
  
  fontstash->PrepareFrame(&vulkanRenderState);
  
  xrUI.PrepareFrame(/*showInfoButton*/ true, /*showBackButton*/ false, &vulkanRenderState);
  
  for (int hand = 0; hand < 2; ++ hand) {
    gltfHandCursors[hand].PrepareFrame();
  }
}

void XRViewerScene::RenderView(VulkanCommandBuffer* cmdBuf, u32 viewIndex, XrTime /*predictedDisplayTime*/, const VulkanFramebuffer& framebuffer, u32 width, u32 height) {
  const auto& api = cmdBuf->pool().device().Api();
  
  commonLogic.PrepareView(viewIndex, /*useSurfaceNormalShading*/ false, &vulkanRenderState);
  
  Eigen::Vector3f backgroundColor(0.1f, 0.1f, 0.1f);  // in sRGB color space
  if (app->IsSRGBRenderTargetUsed()) {
    // Convert the sRGB background color to linear color space
    // (as it will get converted back to sRGB when written to the render target).
    backgroundColor = SRGBToLinear(backgroundColor);
  }
  
  VkRenderPassBeginInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  info.renderPass = app->RenderPass();
  info.framebuffer = framebuffer;
  info.renderArea.extent = {width, height};
  VkClearValue clear_values[2];
  clear_values[0].color.float32[0] = backgroundColor[0];
  clear_values[0].color.float32[1] = backgroundColor[1];
  clear_values[0].color.float32[2] = backgroundColor[2];
  clear_values[0].color.float32[3] = 1.f;
  clear_values[1].depthStencil.depth = 1.f;
  clear_values[1].depthStencil.stencil = 0;  // irrelevant
  info.clearValueCount = 2;
  info.pClearValues = clear_values;
  api.vkCmdBeginRenderPass(*cmdBuf, &info, VK_SUBPASS_CONTENTS_INLINE);
  
  VulkanCmdSetViewportAndScissor(*cmdBuf, cmdBuf->pool().device(), 0, 0, width, height);
  
  CHECK(commonLogic.SupportsLateModelViewProjectionSetting()) << "The XR display code assumes that late model-view-projection setting is supported";
  commonLogic.RenderView(&vulkanRenderState);
  
  for (int hand = 0; hand < 2; ++ hand) {
    if (handIsBeingTracked[hand]) { gltfHandCursors[hand].RenderView(viewIndex, cmdBuf); }
  }
  
  xrUI.RenderView(viewIndex, &vulkanRenderState);
  
  api.vkCmdEndRenderPass(*cmdBuf);
}

void XRViewerScene::SetRenderMatrices(XrTime predictedDisplayTime, XrViewStateFlags viewStateFlags) {
  if (viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT &&
      viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) {
    XRViewerApplication* appC = reinterpret_cast<XRViewerApplication*>(appCallbacks);
    
    // Query the controller poses
    array<bool, 2> have_xrspace_tr_predictedHandPose;
    array<Sophus::SE3f, 2> xrspace_tr_predictedHandPose;
    for (int hand = 0; hand < 2; ++ hand) {
      have_xrspace_tr_predictedHandPose[hand] = GetEigenPose(app->XRSpace(), appC->handPoseSpace[hand], predictedDisplayTime, &xrspace_tr_predictedHandPose[hand]);
    }
    
    // Set render matrices
    for (u32 viewIndex = 0; viewIndex < app->XRViews().size(); ++ viewIndex) {
      const XrView& xrView = app->XRViews()[viewIndex];
      
      const Sophus::SE3f xrspace_tr_view(
          Eigen::Quaternionf(xrView.pose.orientation.w, xrView.pose.orientation.x, xrView.pose.orientation.y, xrView.pose.orientation.z),
          Eigen::Vector3f(xrView.pose.position.x, xrView.pose.position.y, xrView.pose.position.z));
      const Eigen::Matrix4f view_tr_xrspace = xrspace_tr_view.inverse().matrix();
      
      const Eigen::Matrix4f xrspace_tr_xrvideo_matrix = xrspace_tr_xrvideo.matrix();
      
      const Eigen::Matrix4f projection = PerspectiveMatrixOpenXR(xrView.fov.angleLeft, xrView.fov.angleRight, xrView.fov.angleUp, xrView.fov.angleDown, zNear, zFar);
      const Eigen::Matrix4f renderCamera_tr_xrvideo = view_tr_xrspace * xrspace_tr_xrvideo_matrix;
      const Eigen::Matrix4f renderCameraProj_tr_xrvideo = projection * renderCamera_tr_xrvideo;  // this would be called "modelViewProjection" in Computer Graphics
      
      commonLogic.SetModelViewProjection(viewIndex, renderCamera_tr_xrvideo.data(), renderCameraProj_tr_xrvideo.data());
      
      xrUI.SetViewProjection(viewIndex, projection * view_tr_xrspace);
      
      Eigen::Matrix4f leftHandRotation = Eigen::Matrix4f::Identity();
      leftHandRotation.topLeftCorner<3, 3>() = Eigen::AngleAxisf(M_PI, Eigen::Vector3f(0, 0, 1)).toRotationMatrix();
      
      for (int hand = 0; hand < 2; ++ hand) {
        if (handIsBeingTracked[hand]) {
          if (have_xrspace_tr_predictedHandPose[hand]) {
            last_xrspace_tr_predictedHandPose[hand] = xrspace_tr_predictedHandPose[hand];
          }
          Eigen::Matrix4f model = last_xrspace_tr_predictedHandPose[hand].matrix();
          if (hand == 0) {
            model = model * leftHandRotation;
          }
          gltfHandCursors[hand].SetMatrices(viewIndex, model.data(), view_tr_xrspace.data(), projection.data());
        }
      }
    }
  } else {
    LOG(WARNING) << "Either orientation or position is not valid! TODO: Use default orientation / position.";
  }
}

bool ShowXRViewer(bool cacheAllFrames, const char* videoPath, bool verboseDecoding, bool useVulkanDebugLayers, bool useOpenXRDebugLayers) {
  #ifndef __ANDROID__
    SDL_Init(SDL_INIT_AUDIO);
  #endif
  
  auto viewerScene = make_shared<XRViewerScene>(videoPath, cacheAllFrames);
  auto viewerApp = make_shared<XRViewerApplication>();
  
  OpenXRVulkanApplication app;
  if (!app.Initialize("XRVideo Viewer", XR_MAKE_VERSION(1, 0, 0), VK_API_VERSION_1_1, useOpenXRDebugLayers, useVulkanDebugLayers, viewerApp)) { return false; }
  
  viewerScene->SetApplication(&app, viewerApp.get());
  if (!viewerScene->Initialize(verboseDecoding)) {
    LOG(ERROR) << "Failed to initialize scene";
    #ifndef __ANDROID__
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "XRViewer", "Error: Failed to initialize scene", nullptr);
    #endif
    return false;
  }
  
  app.SetScene(viewerScene);
  #ifdef __ANDROID__
    LOG(FATAL) << "Android is not supported by ShowXRViewer() yet";
  #else
    app.RunMainLoop();
  #endif
  
  // Release all callbacks to destruct them before the OpenXRVulkanApplication
  viewerScene.reset();
  viewerApp.reset();
  return true;
}

}

#endif
