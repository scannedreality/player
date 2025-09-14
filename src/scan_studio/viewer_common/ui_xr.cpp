#include "scan_studio/viewer_common/ui_xr.hpp"

#include "scan_studio/viewer_common/audio/audio_sdl.hpp"

#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

XRUI::~XRUI() {
  Destroy();
}

bool XRUI::Initialize(XRVideo* xrVideo, SDLAudio* audio, int viewCount, Shape2DShader* shape2DShader, FontStash* fontstash, int fontstashFont, float fontScaling, VulkanRenderState* renderState) {
  this->xrVideo = xrVideo;
  this->audio = audio;
  this->shape2DShader = shape2DShader;
  
  // Initialize UI elements
  if (!Shape2D::Create(&backgroundDimmer, /*maxVertices*/ 4, /*maxIndices*/ 6, viewCount, shape2DShader, renderState)) { return false; }
  
  if (!basicVideoControlsUI.Initialize(viewCount, shape2DShader, renderState)) { return false; }
  if (!infoOverlayUI.Initialize(/*showTermsAndPrivacyLinks*/ false, viewCount, shape2DShader, fontstash, fontstashFont, fontScaling, renderState)) { return false; }
  
  return true;
}

void XRUI::Destroy() {
  backgroundDimmer.reset();
  basicVideoControlsUI.Destroy();
  infoOverlayUI.Destroy();
}

void XRUI::OnBackClicked(const function<void ()>& callback) {
  backClickedCallback = callback;
}

void XRUI::ClickPress(int handIndex, const Sophus::SE3f& xrspace_tr_hand_at_event, const Sophus::SE3f& xrspace_tr_head_at_event) {
  // Show/hide the controls console, or interact with a control element.
  if (!consoleShown) {
    const Sophus::SE3f head_tr_console(
        Eigen::AngleAxis<float>(180 * M_PI / 180, Eigen::Vector3f(1, 0, 0)).toRotationMatrix(),
        Eigen::Vector3f(0, -0.2f, -0.4f));
    
    consoleShown = true;
    infoOverlayShown = false;
    
    xrspace_tr_console = xrspace_tr_head_at_event * head_tr_console;
    
    // Ensure that the console is upright by taking the "roll" angle out of the rotation.
    // TODO: This works as long as one does not look directly downwards; in that case, the yaw angle is pretty random.
    //       Improve this so that it works for that case as well.
    // Decompose into Euler angles (rotating first around Y, then X, then Z (roll), when starting from xrspace)
    const Eigen::Vector3f eulerAngles = xrspace_tr_console.inverse().rotationMatrix().eulerAngles(2, 0, 1);
    // Compose the rotation again, leaving the roll out
    xrspace_tr_console.setQuaternion((
        Eigen::AngleAxisf(eulerAngles[1], Eigen::Vector3f::UnitX())
        * Eigen::AngleAxisf(eulerAngles[2], Eigen::Vector3f::UnitY())).inverse());
    // If the result is flipped along both axes, correct it by flipping it manually
    if ((xrspace_tr_head_at_event.rotationMatrix() * Eigen::Vector3f(1, 0, 0)).dot(xrspace_tr_console.rotationMatrix() * Eigen::Vector3f(1, 0, 0)) < 0) {
      xrspace_tr_console.setQuaternion((
          Eigen::AngleAxisf(180 * M_PI / 180, Eigen::Vector3f::UnitX())
          * Eigen::AngleAxisf(180 * M_PI / 180, Eigen::Vector3f::UnitY())
          * xrspace_tr_console.unit_quaternion().inverse()
          ).inverse());
    }
    
    return;
  }
  
  // Project the controller aim position onto the 2D plane of the UI
  bool uiElementClicked = false;
  Eigen::Vector2f pos;
  
  if (ProjectPositionOntoUIPlane(xrspace_tr_hand_at_event.translation(), &pos)) {
    // Check for a UI element being clicked
    float factor;
    
    uiElementClicked = true;
    
    if (basicVideoControlsUI.PauseResumeRepeatButtonClicked(pos.x(), pos.y()) && xrVideo->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
      if (repeatButtonShown) {
        xrVideo->Seek(xrVideo->Index().GetVideoStartTimestamp(), /*forwardPlayback*/ true);
        if (audio) { audio->SetPlaybackPosition(0, /*forward*/ true); }
        isPaused = false;
      } else {
        isPaused = !isPaused;
      }
    } else if (basicVideoControlsUI.InfoButtonClicked(pos.x(), pos.y())) {
      infoOverlayShown = !infoOverlayShown;
    } else if (basicVideoControlsUI.BackButtonClicked(pos.x(), pos.y())) {
      if (backClickedCallback) { backClickedCallback(); }
    } else if (basicVideoControlsUI.TimelineClicked(pos.x(), pos.y(), &factor) && xrVideo->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
      const s64 videoStartTimestamp = xrVideo->Index().GetVideoStartTimestamp();
      const s64 videoEndTimestamp = xrVideo->Index().GetVideoEndTimestamp();
      const s64 timestamp = max(videoStartTimestamp, min<s64>(videoEndTimestamp, round(videoStartTimestamp + (1.0 * factor) * (videoEndTimestamp - videoStartTimestamp))));
      
      auto& playbackState = xrVideo->GetPlaybackState();
      playbackState.Lock();
      const bool forwardPlayback = playbackState.PlayingForward();
      playbackState.Unlock();
      
      xrVideo->Seek(timestamp, forwardPlayback);
      if (audio) { audio->SetPlaybackPosition(timestamp - videoStartTimestamp, forwardPlayback); }
    } else {
      uiElementClicked = false;
    }
    
    if (!uiElementClicked && infoOverlayShown) {
      uiElementClicked = infoOverlayUI.TouchOrClickDown(handIndex, pos.x(), pos.y());
    }
  }
  
  if (!uiElementClicked) {
    // No UI element clicked. Hide the console.
    consoleShown = false;
  }
}

void XRUI::Hover(const Sophus::SE3f xrspace_tr_hand[2], const bool active[2], const bool selectPressed[2]) {
  Eigen::Vector2f pos;
  
  basicVideoControlsUI.ResetHoverStates();
  infoOverlayUI.ResetHoverStates();
  
  for (int hand = 0; hand < 2; ++ hand) {
    if (!active[hand]) { continue; }
    
    if (ProjectPositionOntoUIPlane(xrspace_tr_hand[hand].translation(), &pos)) {
      basicVideoControlsUI.AddHover(pos.x(), pos.y());
      if (infoOverlayShown) {
        infoOverlayUI.AddHover(pos.x(), pos.y());
        
        if (selectPressed[hand]) {
          infoOverlayUI.TouchOrClickMove(hand, pos.x(), pos.y());
        }
      }
    }
  }
}

void XRUI::ClickRelease(int handIndex, const Sophus::SE3f& xrspace_tr_hand_at_event) {
  if (consoleShown && infoOverlayShown) {
    Eigen::Vector2f pos;
    if (ProjectPositionOntoUIPlane(xrspace_tr_hand_at_event.translation(), &pos)) {
      infoOverlayUI.TouchOrClickUp(handIndex, pos.x(), pos.y());
    }
  }
}

void XRUI::PrepareFrame(bool showInfoButton, bool showBackButton, VulkanRenderState* renderState) {
  float playbackRatio;
  
  if (xrVideo->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
    PlaybackState& playbackState = xrVideo->GetPlaybackState();
    playbackState.Lock();
    const s64 playbackTime = playbackState.GetPlaybackTime();
    PlaybackMode playbackMode = playbackState.GetPlaybackMode();
    playbackState.Unlock();
    
    const FrameIndex& frameIndex = xrVideo->Index();
    playbackRatio = (playbackTime - frameIndex.GetVideoStartTimestamp()) / (1.0 * (frameIndex.GetVideoEndTimestamp() - frameIndex.GetVideoStartTimestamp()));
    repeatButtonShown =
        playbackMode == PlaybackMode::SingleShot &&
        playbackTime == frameIndex.GetVideoEndTimestamp();
  } else {
    playbackRatio = 0;
    repeatButtonShown = false;
  }
  
  if (consoleShown) {
    constexpr float consoleWidth = 0.55f;
    const Eigen::Vector2f smallButtonSize = Eigen::Vector2f::Constant(0.05f);
    constexpr float timelineHeight = 0.01f;
    
    const Eigen::AlignedBox<float, 2> controlsArea(
        Eigen::Vector2f(-0.5f * consoleWidth, -smallButtonSize.y()),
        Eigen::Vector2f(0.5f * consoleWidth, 0));
    
    basicVideoControlsUI.SetGeometry(
        /*showPauseResumeRepeatButton*/ true, /*showTimeline*/ true, showInfoButton, /*showBackButton*/ showBackButton,
        isPaused, repeatButtonShown, playbackRatio,
        /*minX*/ controlsArea.min().x(), /*maxX*/ controlsArea.max().x(), /*bottomY*/ controlsArea.max().y(), smallButtonSize, timelineHeight,
        renderState);
    
    Eigen::AlignedBox<float, 2> uiArea;
    if (infoOverlayShown) {
      uiArea = Eigen::AlignedBox<float, 2>(
          Eigen::Vector2f(controlsArea.min().x(), -10 * smallButtonSize.y()),
          Eigen::Vector2f(controlsArea.max().x(), controlsArea.max().y()));
    } else {
      uiArea = controlsArea;
    }
    
    array<Eigen::Vector2f, 4> backgroundVertices;
    const array<u16, 6> backgroundIndices = {0, 3, 1, 1, 3, 2};
    
    backgroundVertices[0] = uiArea.min();
    backgroundVertices[1] = Eigen::Vector2f(uiArea.max().x(), uiArea.min().y());
    backgroundVertices[2] = uiArea.max();
    backgroundVertices[3] = Eigen::Vector2f(uiArea.min().x(), uiArea.max().y());
    backgroundDimmer->SetGeometry(backgroundVertices.size(), backgroundVertices.data(), backgroundIndices.size(), backgroundIndices.data(), renderState);
    
    // About overlay
    if (infoOverlayShown) {
      const float helpTextFontSize = 30;
      const float titleFontSize = helpTextFontSize * 3.0f;
      
      infoOverlayUI.SetGeometry(
          /*minX*/ uiArea.min().x(), /*minY*/ uiArea.min().y(), /*maxX*/ uiArea.max().x(), /*maxY*/ uiArea.max().y(),
          smallButtonSize, titleFontSize, helpTextFontSize, renderState);
    }
  }
}

void XRUI::RenderView(int viewIndex, VulkanRenderState* renderState) {
  if (consoleShown) {
    backgroundDimmer->RenderView(Eigen::Vector4f(0, 0, 0, 0.7f), viewIndex, shape2DShader, renderState);
    basicVideoControlsUI.RenderView(viewIndex, /*distinctHoverColors*/ true, renderState);
    if (infoOverlayShown) {
      infoOverlayUI.RenderView(viewIndex, /*distinctHoverColors*/ true, renderState);
    }
  }
}

void XRUI::SetViewProjection(int viewIndex, const Eigen::Matrix4f& viewProjection) {
  const Eigen::Matrix4f modelViewProjection = viewProjection * xrspace_tr_console.matrix();
  
  backgroundDimmer->SetModelViewProjection(viewIndex, modelViewProjection.data());
  basicVideoControlsUI.SetModelViewProjection(viewIndex, modelViewProjection.data());
  infoOverlayUI.SetModelViewProjection(viewIndex, modelViewProjection.data());
}

bool XRUI::ProjectPositionOntoUIPlane(const Eigen::Vector3f& xrspace_position, Eigen::Vector2f* uiPlane_position) {
  constexpr float depthInteractionTolerance = 0.025f;
  
  const Eigen::Vector3f consolePos = xrspace_tr_console.inverse() * xrspace_position;
  
  if (fabs(consolePos.z()) <= depthInteractionTolerance) {
    *uiPlane_position = consolePos.topRows<2>();
    return true;
  }
  return false;
}

}
