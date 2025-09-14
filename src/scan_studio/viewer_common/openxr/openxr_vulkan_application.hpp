#pragma once
#ifdef HAVE_OPENXR

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#ifdef __ANDROID__
  #include <android_native_app_glue.h>
#endif

#include <libvis/vulkan/device.h>
#include <libvis/vulkan/instance.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/openxr/instance.hpp"
#include "scan_studio/viewer_common/openxr/session.hpp"
#include "scan_studio/viewer_common/openxr/space.hpp"
#include "scan_studio/viewer_common/openxr/swapchain.hpp"

namespace scan_studio {
using namespace vis;

class OpenXRVulkanApplication;

/// Callbacks class that is given to OpenXRVulkanApplication to handle events during the application lifetime.
class OpenXRApplicationCallbacks {
 public:
  virtual inline ~OpenXRApplicationCallbacks() {}
  
  /// Called before initialization of OpenXR.
  /// Must add all desired OpenXR instance extensions that should be enabled to the `extensions` vector.
  /// Implementations can access app->XRInstance() to check which extensions are supported.
  /// Must return true on success, false if initialization should be aborted.
  /// The default implementation does not enable any extensions.
  virtual inline bool SpecifyOpenXRInstanceExtensions(
      bool useOpenXRDebugLayers,
      vector<string>* extensions) {
    (void) useOpenXRDebugLayers;
    (void) extensions;
    return true;
  }
  
  /// Called before initialization of the logical Vulkan device.
  /// Given the selected `vkPhysicalDevice`, may modify the (other) parameters, as well as app->VKDevice(), as desired to enable features or device extensions.
  /// Note that vkDeviceExtensions is already pre-populated with the Vulkan device extensions requested by the OpenXR runtime.
  /// The default implementation does not enable any features or additional device extensions.
  virtual inline bool SpecifyVulkanDeviceFeaturesAndExtensions(
      const VulkanPhysicalDevice& vkPhysicalDevice,
      VkPhysicalDeviceFeatures* enabled_features,
      vector<string>* vkDeviceExtensions,
      const void** deviceCreateInfoPNext) {
    (void) vkPhysicalDevice;
    (void) enabled_features;
    (void) vkDeviceExtensions;
    (void) deviceCreateInfoPNext;
    return true;
  }
  
  /// Called before initialization of the OpenXR reference space type and view configuration type.
  /// Must select the desired values.
  virtual bool ChooseSpaceAndView(
      const vector<XrReferenceSpaceType>& supportedReferenceSpaceTypes,
      XrReferenceSpaceType* chosenReferenceSpaceType,
      const vector<XrViewConfigurationType>& supportedViewConfigurationTypes,
      XrViewConfigurationType* chosenViewConfigurationType) = 0;
  
  /// Called before the initialization of the render pass.
  /// Must choose the number of msaa samples.
  virtual void ChooseMsaaSamples(
      const VulkanPhysicalDevice& vkPhysicalDevice,
      VkSampleCountFlagBits* msaaSamples) = 0;
  
  /// Called after the initialization of OpenXR and Vulkan finished.
  /// The application can do its own initializations here.
  /// For example, this is the right place to create the OpenXR actions for the application.
  virtual bool Initialize() {
    return true;
  }
  
  /// Called when the currently used interaction profiles changed,
  /// for example, at the application start, or after a new controller is connected.
  /// Attention: The OpenXRApplicationCallbacks implementation must not assume that a scene is present when this is called,
  ///            since on the first time, it is called immediately after initialization before a scene is set.
  virtual inline void OnCurrentInteractionProfilesChanged() {}
  
  /// Called if there is a fatal error (e.g., initialization failure).
  /// May for example show a dialog box to the user with the message text.
  /// Note that the OpenXRVulkanApplication already logs the error message to the loguru log before calling OnError().
  ///
  /// Note that OnError() is *not* called if initialization is aborted because the OpenXRApplicationCallbacks object
  /// returned false in a callback (because in such cases, OpenXRVulkanApplication does not know the underlying error reason);
  /// in this case, the OpenXRApplicationCallbacks object should log and show the error message itself (if desired).
  virtual inline void OnError(const string& message) {
    (void) message;
  }
  
  inline void SetApplication(OpenXRVulkanApplication* app) {
    this->app = app;
  }
  
 protected:
  OpenXRVulkanApplication* app = nullptr;
};

/// Callbacks class that is given to OpenXRVulkanApplication to do the rendering, handle the input, etc.
/// Basically, may define a "scene" in the XR application.
class OpenXRSceneCallbacks {
 public:
  virtual inline ~OpenXRSceneCallbacks() {}
  
  /// Called upon entering the scene.
  ///
  /// May for example initialize graphics objects such as for example vertex and index buffers,
  /// but preferably only if this can be done very quickly (in order to prevent the application from lagging).
  /// Only the first scene in the application may spend a longer time loading here, as in this case,
  /// the application has not rendered any frame yet, so the OpenXR runtime will likely show its own
  /// loading screen until then.
  virtual inline void OnEnterScene() {}
  
  /// Called upon leaving the scene.
  virtual inline void OnLeaveScene() {}
  
  /// Called before rendering each frame to poll the inputs (actions).
  virtual void PollActions(XrTime predictedDisplayTime) = 0;
  
  /// Called before rendering each frame.
  /// Must return the Z-range used for rendering the frame in *nearZ and *farZ.
  virtual void PrepareFrame(VulkanCommandBuffer* cmdBuf, XrTime predictedDisplayTime, float* nearZ, float* farZ) = 0;
  
  /// Called for rendering the view with the given index.
  virtual void RenderView(VulkanCommandBuffer* cmdBuf, u32 viewIndex, XrTime predictedDisplayTime, const VulkanFramebuffer& framebuffer, u32 width, u32 height) = 0;
  
  /// Called as late as possible before each frame is rendered before submitting the Vulkan command buffer.
  /// This allows to update the poses used for rendering with the latest (and thus best, since it might improve over time) available data.
  /// To reduce boilerplate code, the OpenXRVulkanApplication queries the poses of the views before calling this, which are available via app->XRViews() and `viewStateFlags`.
  virtual inline void SetRenderMatrices(XrTime predictedDisplayTime, XrViewStateFlags viewStateFlags) {
    (void) predictedDisplayTime;
    (void) viewStateFlags;
  }
  
  inline void SetApplication(OpenXRVulkanApplication* app, OpenXRApplicationCallbacks* appCallbacks) {
    this->app = app;
    this->appCallbacks = appCallbacks;
  }
  
 protected:
  OpenXRVulkanApplication* app = nullptr;
  OpenXRApplicationCallbacks* appCallbacks = nullptr;
};

/// Creates and manages an OpenXR instance and session, using Vulkan for rendering.
/// Provides roughly analogous functionality to what a "render window" does on the desktop.
/// Forwards the actual rendering and input handling to an OpenXRApplicationCallbacks instance
/// that defines the current "scene" of the XR application.
///
/// To use this class:
/// - First call Initialize()
/// - Then do any other required initializations (such as initializing OpenXRSceneCallbacks implementations)
/// - Then call SetScene() to set the initial scene
/// - Use RunMainLoop()
/// After the main loop ends, make sure to destroy the callbacks first if they still hold any
/// OpenXR or Vulkan objects, before destroying the OpenXRVulkanApplication object.
class OpenXRVulkanApplication {
 public:
  /// Initializes OpenXR and Vulkan.
  /// For openXRVersion, use for example: XR_MAKE_VERSION(1, 0, 0).
  /// For suggestedVulkanVersion, use for example: VK_API_VERSION_1_1.
  ///   However, keep in mind that the min/max Vulkan version requirement of OpenXR might force OpenXRVulkanApplication to use a different version.
  ///   The actually chosen version can be queried with VulkanVersion().
  ///   Also note that with some (early) version of the Steam OpenXR runtime, I needed to use at least Vulkan 1.1 to get it to work.
  /// Returns true on success, false on failure.
  bool Initialize(
      const char* applicationName,
      XrVersion openXRVersion,
      u32 suggestedVulkanVersion,
      bool useOpenXRDebugLayers,
      bool useVulkanDebugLayers,
      const shared_ptr<OpenXRApplicationCallbacks>& appCallbacks);
  
  /// Runs the main loop until the program exits, polling input and doing rendering from this thread.
  void RunMainLoop(
      #ifdef __ANDROID__
        struct android_app* androidApp
      #endif
  );
  
  /// May be called while the main loop is active to make it quit after the current iteration ends.
  /// Note that this means that the application does not exit immediately upon calling Quit().
  void Quit();
  
  /// Changes the scene callbacks object of the application to the given new object.
  /// This may be used to switch to a different "scene" or "mode" of the XR application.
  ///
  /// Important: To allow for calling this function at any time, the scene is not switched
  /// immediately. Instead, it will be switched before the next frame starts. This is to
  /// avoid calling some of the frame's callbacks for one scene, and then switching to
  /// another scene for the rest of the callbacks (which might then behave wrongly and
  /// possibly crash the app if they expect all frame callbacks to be called in order).
  /// This means that even if you call SetScene(), the old scene must be prepared to
  /// finish rendering the current frame (if a frame is currently ongoing).
  void SetScene(const shared_ptr<OpenXRSceneCallbacks>& callbacks);
  
  inline void SetEnvironmentBlendMode(XrEnvironmentBlendMode mode) { environmentBlendMode = mode; }
  inline XrEnvironmentBlendMode EnvironmentBlendMode() const { return environmentBlendMode; }
  
  /// Adds a composition layer that will be displayed from the next rendered frame on.
  ///
  /// The application's own layer has an index of zero at the start (with no other layers added).
  /// Inserting an additional layer at index zero will have that layer displayed before the application's own layer
  /// (i.e., the application's own layer is drawn on top); inserting an additional layer at index one will
  /// have it dispayed after the application's own layer (i.e., the layer is drawn over the application).
  ///
  /// The content at the given XrCompositionLayerBaseHeader pointer must remain valid as long
  /// as the layer is being used for rendering.
  void AddCompositionLayer(int insertionIndex, XrCompositionLayerBaseHeader* layer);
  void RemoveCompositionLayer(XrCompositionLayerBaseHeader* layer);
  
  inline XrCompositionLayerProjection* ApplicationCompositionLayer() { return &applicationLayer; }
  
  int MaxFrameInFlightCount() const { return maxFrameInFlightCount; }
  int CurrentFrameInFlightIndex() const { return currentFrameInFlightIndex; }
  
  const OpenXRInstance& XRInstance() const { return xrInstance; }
  OpenXRInstance& XRInstance() { return xrInstance; }
  
  const OpenXRSession& XRSession() const { return xrSession; }
  OpenXRSession& XRSession() { return xrSession; }
  
  const OpenXRSpace& XRSpace() const { return xrSpace; }
  const OpenXRSpace& XRViewSpace() const { return xrViewSpace; }
  
  const VulkanDevice& VKDevice() const { return vkDevice; }
  VulkanDevice& VKDevice() { return vkDevice; }
  
  const VulkanRenderPass& RenderPass() const { return renderPass; }
  
  bool IsSRGBRenderTargetUsed() const { return isSRGBRenderTargetUsed; }
  
  const vector<XrView>& XRViews() const { return xrViews; }
  
  const VulkanCommandPool& GraphicsCommandPool() const { return graphicsCommandPool; }
  VulkanCommandPool& GraphicsCommandPool() { return graphicsCommandPool; }
  
  /// Returns the Vulkan version that is being used (selected based on suggestedVulkanVersion given to Initialize(),
  /// as well as the min/max Vulkan version requirement of OpenXR).
  inline u32 VulkanVersion() const { return vulkanVersion; }
  
  /// Returns the current OpenXR session state.
  inline XrSessionState GetXrSessionState() const { return xrSessionState; }
  
 private:
  // The initialization functions must be called in the order in which they are defined.
  bool InitializeOpenXRInstance(const char* applicationName, XrVersion version, bool useOpenXRDebugLayers);
  bool InitializeVulkanInstance(u32 suggestedVersion, bool useVulkanDebugLayers);
  bool InitializeVulkanDevice();
  bool InitializeOpenXRSessionSpaceAndViewConfig();
  bool InitializeRenderPassAndSwapChains();
  bool InitializeGraphicObjects();
  bool InitializeCompositionLayers();
  
  // OpenXR rendering functionality
  void RenderFrame();
  bool RenderLayer(XrTime predictedDisplayTime);
  
  /// Logs the error, calls callbacks->OnError(errorMsg), and always returns false (to allow for shorter code: return OnError(errorMsg);)
  bool OnError(const string& errorMsg);
  
  // Vulkan instance & device
  VulkanInstance vkInstance;
  u32 vulkanVersion;
  unique_ptr<VulkanPhysicalDevice> vkPhysicalDevice;
  VulkanDevice vkDevice;
  int selected_graphics_queue_family_index;
  VulkanQueue* graphicsQueue;
  int xrQueueIndex;
  int maxFrameInFlightCount;
  int currentFrameInFlightIndex;
  
  // OpenXR instance & session
  // (must be below Vulkan instance & device since at least the xrSession has Vulkan objects allocated)
  OpenXRInstance xrInstance;
  OpenXRSession xrSession;
  OpenXRSpace xrSpace;
  OpenXRSpace xrViewSpace;
  
  // OpenXR state
  XrEnvironmentBlendMode environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;  // can be set on each xrEndFrame(), stored here for convenience
  XrCompositionLayerProjection applicationLayer;
  vector<XrCompositionLayerBaseHeader*> compositionLayers;
  XrSessionState xrSessionState = XR_SESSION_STATE_UNKNOWN;
  
  // OpenXR extensions
  constexpr static bool usingExtension_XR_KHR_composition_layer_depth = false;  // TODO: Implement use of this extension
  
  // Views & swapchains
  VulkanRenderPass renderPass;
  XrViewConfigurationType viewConfigurationType;
  bool isSRGBRenderTargetUsed;
  vector<OpenXRSwapchain> xrSwapchains;
  vector<XrView> xrViews;
  vector<XrCompositionLayerProjectionView> projViews;
  vector<XrCompositionLayerDepthInfoKHR> depthViews;
  
  // Vulkan objects
  VulkanCommandPool graphicsCommandPool;
  vector<VulkanCommandBuffer> commandBuffers;
  
  // Application and scene callbacks
  shared_ptr<OpenXRApplicationCallbacks> appCallbacks;
  shared_ptr<OpenXRSceneCallbacks> sceneCallbacks;
  shared_ptr<OpenXRSceneCallbacks> nextSceneCallbacks;
  
  // State
  atomic<bool> quitRequested;
};

}

#endif
