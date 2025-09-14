#pragma once

#include <Eigen/Geometry>

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
struct RenderState;
class XRVideo;

class FlatscreenUI {
 public:
  bool Initialize(XRVideo* xrVideo, SDLAudio* audio, RenderState* renderState, const string& videoTitle, bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks);
  void Deinitialize();
  
  void OnBackClicked(const function<void()>& callback);
  
  void Update(
      s64 nanosecondsSinceLastFrame, float playbackRatio,
      bool showRepeatButton, bool showInfoButton, bool showBackButton, bool showBufferingIndicator, float bufferingPercent,
      int width, int height, float xdpi, float ydpi, RenderState* renderState);
  void RenderView(RenderState* renderState);
  
  /// Returns true if the given position is over an interactive element (and thus a suitable cursor should be displayed).
  bool Hover(float x, float y);
  
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickDown(float x, float y);
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickMove(float x, float y);
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickUp(float x, float y);
  
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool WheelRotated(float degrees);
  
  inline bool IsPaused() const { return isPaused || infoOverlayShown; }
  inline void SetPaused(bool state) { isPaused = state; }
  
 private:
  void SetModelViewProjection(const Eigen::Matrix4f& matrix);
  
  function<void()> backClickedCallback;
  
  // FontStash
  FontStashShader fontstashShader;
  unique_ptr<FontStash> fontstash;
  int fontstashFont;
  
  // Shape2D
  Shape2DShader shape2DShader;
  
  // UI elements
  string videoTitleText;
  unique_ptr<Text2D> videoTitle;
  // unique_ptr<Text2D> betaTag;
  
  unique_ptr<Text2D> helpTextRotate;
  unique_ptr<Text2D> helpTextMove;
  unique_ptr<Text2D> helpTextZoom;
  
  unique_ptr<Text2D> helpTextRotateFinger;
  unique_ptr<Text2D> helpTextMoveFinger;
  unique_ptr<Text2D> helpTextZoomFinger;
  
  unique_ptr<Text2D> helpTextRotateSeparator;
  unique_ptr<Text2D> helpTextMoveSeparator;
  unique_ptr<Text2D> helpTextZoomSeparator;
  
  unique_ptr<Text2D> helpTextRotateMouse;
  unique_ptr<Text2D> helpTextMoveMouse;
  unique_ptr<Text2D> helpTextZoomMouse;
  
  BasicVideoControlsUI basicVideoControlsUI;
  InfoOverlayUI infoOverlayUI;
  
  unique_ptr<Shape2D> overlayBackgroundDimmer;
  
  struct BufferingIndicator {
    static constexpr int kSegments = 32;
    unique_ptr<Text2D> text;
    unique_ptr<Shape2D> shape;
    float angle = 0;
    bool show = false;
  } bufferingIndicator;
  
  int width = -1;
  int height = -1;
  
  // State
  bool isPaused = false;
  bool repeatButtonShown = false;
  bool infoOverlayShown = false;
  
  bool showMouseControlsHelp;
  bool showTouchControlsHelp;
  XRVideo* xrVideo;  // not owned
  SDLAudio* audio;  // not owned
};

}
