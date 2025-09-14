#pragma once

#include <Eigen/Geometry>

#ifndef __ANDROID__
#include <SDL.h>
#endif

#include <libvis/util/window_callbacks.h>
#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/3d_orbit_view_control.hpp"
#include "scan_studio/viewer_common/common.hpp"
#include "scan_studio/viewer_common/ui_flatscreen.hpp"

namespace scan_studio {
using namespace vis;

class RenderWindowSDL;

bool ShowDesktopViewer(
    RendererType rendererType, int defaultWindowWidth, int defaultWindowHeight,
    bool preReadCompleteFile, bool cacheAllFrames,
    const char* videoPath, const char* videoTitle,
    bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks,
    bool verboseDecoding, bool useVulkanDebugLayers);

/// Application logic used only by the flatscreen (desktop / mobile) display mode, not by the XR display mode.
/// For example: mouse input handling, possible two-dimensional UI element display, etc.
class RenderCallbacks : public WindowCallbacks {
 public:
  RenderCallbacks(
      RendererType rendererType,
      bool preReadCompleteFile, bool cacheAllFrames, RenderWindowSDL* renderWindow,
      const char* videoPath, const char* videoTitle,
      bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks,
      bool verboseDecoding);
  
  #ifdef __ANDROID__
  void SetEGLConfig(void* config);
  #endif
  
  /// Must be called on Android before Initialize() since the Android path does not use SDL, which is
  /// otherwise used to get the DPI during Initialize().
  void SetDPI(float xdpi, float ydpi);
  
  virtual void PreInitialize(VulkanPhysicalDevice* physical_device, VkSampleCountFlags* msaaSamples, VkPhysicalDeviceFeatures* features_to_enable, vector<string>* deviceExtensions, const void** deviceCreateInfoPNext) override;
  virtual bool Initialize() override;
  virtual void Deinitialize() override;
  
  virtual bool InitializeSurfaceDependent() override;
  virtual void DeinitializeSurfaceDependent() override;
  
  virtual void Resize(int width, int height) override;
  
  virtual void Render(int imageIndex) override;
  
  virtual void MouseDown(MouseButton button, int x, int y, int clickCount) override;
  virtual void MouseMove(int x, int y) override;
  virtual void MouseUp(MouseButton button, int x, int y, int clickCount) override;
  virtual void WheelRotated(float degrees, Modifier modifiers) override;
  
  virtual void FingerDown(s64 finger_id, float x, float y) override;
  virtual void FingerMove(s64 finger_id, float x, float y) override;
  virtual void FingerUp(s64 finger_id, float x, float y) override;
  
  virtual void KeyPressed(int key, Modifier modifiers) override;
  
  inline RendererType GetRendererType() const { return rendererType; }
  
 private:
  void InitializeImpl();
  static void InitializeImplStatic(int thisInt);
  void DeinitializeImpl();
  static void DeinitializeImplStatic(int thisInt);
  
  bool InitializeSurfaceDependentImpl();
  static void InitializeSurfaceDependentImplStatic(int thisInt);
  void DeinitializeSurfaceDependentImpl();
  static void DeinitializeSurfaceDependentImplStatic(int thisInt);
  
  void RenderImpl(int imageIndex);
  static void RenderImplStatic(int thisInt, int imageIndex);
  
  int width = 0;
  int height = 0;
  
  float xdpi;
  float ydpi;
  
  #ifdef __ANDROID__
    void* eglConfig;
  #else
    SDL_Cursor* defaultCursor = nullptr;
    SDL_Cursor* handCursor = nullptr;
    
    SDL_Cursor* currentCursor = nullptr;
  #endif
  
  // Input handling for 3D view
  OrbitViewParameters view;
  MouseOrbitViewController mouseViewController;
  TouchOrbitViewController touchViewController;
  double minDepth = 0.1;
  double maxDepth = 50.0;
  
  bool initialViewInitialized = false;
  OrbitViewParameters initialView;
  bool autoViewAnimation = true;
  s64 viewAnimationTimeNanoseconds = 0;
  
  TouchGestureDetector touchGestureDetector;
  vector<s64> uiInteractionFingerIDs;
  
  RenderWindowSDL* renderWindow = nullptr;
  
  // Renderer type: OpenGL or Vulkan
  RendererType rendererType;
  
  // Application logic
  bool preReadCompleteFile;
  string videoPath;
  string videoTitle;
  s64 applicationTimeNanoseconds = 0;
  TimePoint lastFrameTime;
  bool showMouseControlsHelp;
  bool showTouchControlsHelp;
  bool showTermsAndPrivacyLinks;
  bool verboseDecoding;
  bool useSurfaceNormalShading = false;
  FlatscreenUI flatscreenUI;
  ViewerCommon commonLogic;
};

}
