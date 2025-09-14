#pragma once

#include <unordered_map>

#include <Eigen/Geometry>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/viewer_common/gfx/shape2d.hpp"
#include "scan_studio/viewer_common/gfx/shape2d_shader.hpp"
#include "scan_studio/viewer_common/gfx/text2d.hpp"
#include "scan_studio/viewer_common/gfx/fontstash.hpp"
#include "scan_studio/viewer_common/gfx/fontstash_shader.hpp"

namespace scan_studio {
using namespace vis;

class BasicVideoControlsUI {
 public:
  bool Initialize(int viewCount, Shape2DShader* shape2DShader, RenderState* renderState);
  void Destroy();
  
  void SetGeometry(
      // Visibility
      bool showPauseResumeRepeatButton, bool showTimeline, bool showInfoButton, bool showBackButton,
      // Current state
      bool isPaused, bool showRepeatButton, float playbackRatio,
      // Dimensions
      float minX, float maxX, float bottomY, const Eigen::Vector2f& smallButtonSize, float timelineHeight,
      // Render state
      RenderState* renderState);
  
  void ResetHoverStates();
  
  /// Adds hover states for a cursor at (x, y).
  /// Call ResetHoverStates() first and then AddHover() for each cursor (multiple cursors are used for XR controllers).
  /// Returns true if any contained UI element is hovered by the current cursor, false otherwise.
  bool AddHover(float x, float y);
  
  bool PauseResumeRepeatButtonClicked(float x, float y);
  bool TimelineClicked(float x, float y, float* factor);
  bool InfoButtonClicked(float x, float y);
  bool BackButtonClicked(float x, float y);
  
  void RenderView(int viewIndex, bool distinctHoverColors, RenderState* renderState);
  
  void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData);
  
 private:
  unique_ptr<Shape2D> pauseResumeRepeatButton;
  unique_ptr<Shape2D> timeline;
  unique_ptr<Shape2D> timelineMarkedRange;
  unique_ptr<Shape2D> infoButton;
  unique_ptr<Shape2D> backButton;
  
  Eigen::AlignedBox<float, 2> pauseResumeRepeatButtonBox;
  bool pauseResumeButtonHovered = false;
  Eigen::AlignedBox<float, 2> timelineInteractiveAreaBox;
  bool timelineHovered = false;
  Eigen::AlignedBox<float, 2> infoButtonBox;
  bool infoButtonHovered = false;
  Eigen::AlignedBox<float, 2> backButtonBox;
  bool backButtonHovered = false;
  
  bool showPauseResumeRepeatButton = false;
  bool showTimeline = false;
  bool showInfoButton = false;
  bool showBackButton = false;
  
  Shape2DShader* shape2DShader;  // not owned
};

class InfoOverlayUI {
 public:
  bool Initialize(bool showLegalNoticeAndPrivacyLinks, int viewCount, Shape2DShader* shape2DShader, FontStash* fontstash, int fontstashFont, float fontScaling, RenderState* renderState);
  void Destroy();
  
  void SetGeometry(
      // Dimensions
      float minX, float minY, float maxX, float maxY,
      const Eigen::Vector2f& smallButtonSize,
      float titleFontSize, float helpTextFontSize,
      // Render state
      RenderState* renderState);
  
  void ResetHoverStates();
  
  /// Adds hover states for a cursor at (x, y).
  /// Call ResetHoverStates() first and then AddHover() for each cursor (multiple cursors are used for XR controllers).
  /// Returns true if any contained UI element is hovered by the current cursor, false otherwise.
  bool AddHover(float x, float y);
  
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickDown(s64 cursorId, float x, float y);
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickMove(s64 cursorId, float x, float y);
  /// Returns true if the UI handles the event (and it should not be handled by the 3D view), false otherwise.
  bool TouchOrClickUp(s64 cursorId, float x, float y);
  
  void WheelRotated(float degrees);
  
  void RenderView(int viewIndex, bool distinctHoverColors, RenderState* renderState);
  
  void SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData);
  
 private:
  unique_ptr<Text2D> aboutTitle;
  unique_ptr<Text2D> aboutDescription;
  unique_ptr<Text2D> aboutLegalNotice;
  unique_ptr<Text2D> aboutPrivacy;
  unique_ptr<Text2D> aboutOpenSourceLicenses;
  unique_ptr<Text2D> aboutOpenSourceLicenses2;
  int selectedOpenSourceComponent = 0;
  unique_ptr<Text2D> aboutOpenSourceComponent;
  
  unique_ptr<Shape2D> nextOpenSourceComponentArrow;
  Eigen::AlignedBox<float, 2> nextOpenSourceComponentArrowBox;
  bool nextOpenSourceComponentArrowHovered = false;
  
  unique_ptr<Shape2D> prevOpenSourceComponentArrow;
  Eigen::AlignedBox<float, 2> prevOpenSourceComponentArrowBox;
  bool prevOpenSourceComponentArrowHovered = false;
  
  Eigen::AlignedBox<float, 2> licenseTextArea;
  vector<unique_ptr<Text2D>> licenseTextLines;
  vector<float> licenseTextFadeStates;
  int visibleLicenseTextLines = 0;
  float licenseTextScroll = 0;
  float licenseTextMaxScroll = 0;
  float licenseTextScrollStep;
  
  bool showLegalNoticeAndPrivacyLinks;
  Eigen::AlignedBox<float, 2> legalNoticeBox;
  Eigen::AlignedBox<float, 2> privacyBox;
  
  // Input handling
  unordered_map<s64, Eigen::Vector2f> cursorLastDragPos;
  
  // External objects, not owned
  FontStash* fontstash;
  int fontstashFont;
  float fontScaling;
  Shape2DShader* shape2DShader;
};

}
