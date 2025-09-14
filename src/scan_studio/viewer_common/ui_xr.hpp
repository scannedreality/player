#pragma once

#include "scan_studio/viewer_common/pi.hpp"

#include <sophus/se3.hpp>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/shape2d.hpp"
#include "scan_studio/viewer_common/gfx/shape2d_shader.hpp"
#include "scan_studio/viewer_common/gfx/text2d.hpp"
#include "scan_studio/viewer_common/gfx/fontstash.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

#include "scan_studio/viewer_common/ui_common.hpp"

namespace scan_studio {
using namespace vis;

class SDLAudio;
struct VulkanRenderState;
class XRVideo;

/// TODO: It would probably be good to re-implement this GUI on top of UGUI
class XRUI {
 public:
  ~XRUI();
  
  bool Initialize(XRVideo* xrVideo, SDLAudio* audio, int viewCount, Shape2DShader* shape2DShader, FontStash* fontstash, int fontstashFont, float fontScaling, VulkanRenderState* renderState);
  void Destroy();
  
  void OnBackClicked(const function<void()>& callback);
  
  // Input
  void ClickPress(int handIndex, const Sophus::SE3f& xrspace_tr_hand_at_event, const Sophus::SE3f& xrspace_tr_head_at_event);
  void Hover(const Sophus::SE3f xrspace_tr_hand[2], const bool active[2], const bool selectPressed[2]);
  void ClickRelease(int handIndex, const Sophus::SE3f& xrspace_tr_hand_at_event);
  
  // Rendering
  void PrepareFrame(bool showInfoButton, bool showBackButton, VulkanRenderState* renderState);
  void RenderView(int viewIndex, VulkanRenderState* renderState);
  void SetViewProjection(int viewIndex, const Eigen::Matrix4f& viewProjection);
  
  inline bool IsPaused() const { return isPaused; }
  inline void SetPaused(bool state) { isPaused = state; }
  
  inline bool IsConsoleShown() const { return consoleShown; }
  inline void SetConsoleShown(bool state) { consoleShown = state; }
  
 private:
  bool ProjectPositionOntoUIPlane(const Eigen::Vector3f& xrspace_position, Eigen::Vector2f* uiPlane_position);
  
  bool consoleShown = false;
  
  /// If consoleShown is true, this gives the console pose.
  /// The origin of the console is defined to be at its bottom-center.
  /// +x goes to the right, +y goes down.
  Sophus::SE3f xrspace_tr_console;
  
  function<void()> backClickedCallback;
  
  // UI elements
  unique_ptr<Shape2D> backgroundDimmer;
  
  BasicVideoControlsUI basicVideoControlsUI;
  InfoOverlayUI infoOverlayUI;
  
  // State
  bool isPaused = false;
  bool repeatButtonShown = false;
  bool infoOverlayShown = false;
  
  Shape2DShader* shape2DShader;  // not owned
  XRVideo* xrVideo;  // not owned
  SDLAudio* audio;  // not owned
};

}
