#include "scan_studio/viewer_common/ui_flatscreen.hpp"

#include <loguru.hpp>

#include <libvis/io/filesystem.h>
#include <libvis/io/globals.h>

#include "scan_studio/viewer_common/audio/audio_sdl.hpp"

#include "scan_studio/viewer_common/gfx/fontstash.hpp"

#include "scan_studio/viewer_common/xrvideo/xrvideo.hpp"

#include "scan_studio/viewer_common/pi.hpp"
#include "scan_studio/viewer_common/render_state.hpp"

namespace scan_studio {

bool FlatscreenUI::Initialize(XRVideo* xrVideo, SDLAudio* audio, RenderState* renderState, const string& videoTitle, bool showMouseControlsHelp, bool showTouchControlsHelp, bool showTermsAndPrivacyLinks) {
  this->xrVideo = xrVideo;
  this->audio = audio;
  this->showMouseControlsHelp = showMouseControlsHelp;
  this->showTouchControlsHelp = showTouchControlsHelp;
  
  // Initialize FontStash
  if (!fontstashShader.Initialize(/*enableDepthTesting*/ false, renderState)) { return false; }
  fontstash.reset(FontStash::Create(/*textureWidth*/ 1024, /*textureHeight*/ 512, &fontstashShader, renderState));
  if (!fontstash) { return false; }
  
  const fs::path droidSansPath =
      #ifdef TARGET_OS_IOS
        "DroidSans.ttf";  // we get a flat directory structure for resources on iOS using https://cmake.org/cmake/help/latest/prop_tgt/RESOURCE.html
      #else
        MaybePrependAppPath(fs::path("resources") / "fonts" / "droid-sans" / "DroidSans.ttf", /*isRelativeToAppPath*/ true);
      #endif
  unique_ptr<InputStream> droidSansStream = OpenAssetUnique(droidSansPath, /*isRelativeToAppPath*/ false);
  if (droidSansStream == nullptr) {
    LOG(ERROR) << "Failed to load DroidSans.ttf file from " << droidSansPath;
    return false;
  }
  fontstashFont = fontstash->LoadFont("DroidSans", std::move(droidSansStream));
  if (fontstashFont == FONS_INVALID) { LOG(ERROR) << "Failed to load DroidSans.ttf"; return false; }
  
  // Initialize Shape2D shader
  if (!shape2DShader.Initialize(/*enableDepthTesting*/ false, renderState)) { return false; }
  
  // Initialize UI elements
  videoTitleText = videoTitle;
  if (!Text2D::Create(&this->videoTitle, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  // if (!Text2D::Create(&betaTag, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  
  if (!Text2D::Create(&helpTextRotate, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextMove, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextZoom, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextRotateFinger, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextMoveFinger, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextZoomFinger, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextRotateSeparator, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextMoveSeparator, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextZoomSeparator, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextRotateMouse, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextMoveMouse, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  if (!Text2D::Create(&helpTextZoomMouse, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  
  if (!basicVideoControlsUI.Initialize(/*viewCount*/ 1, &shape2DShader, renderState)) { return false; }
  if (!infoOverlayUI.Initialize(showTermsAndPrivacyLinks, /*viewCount*/ 1, &shape2DShader, fontstash.get(), fontstashFont, /*fontScaling*/ 1, renderState)) { return false; }
  
  if (!Shape2D::Create(&overlayBackgroundDimmer, /*maxVertices*/ 4, /*maxIndices*/ 6, /*viewCount*/ 1, &shape2DShader, renderState)) { return false; }
  
  if (!Shape2D::Create(&bufferingIndicator.shape, /*maxVertices*/ 2 * BufferingIndicator::kSegments, /*maxIndices*/ 2 * 3 * (BufferingIndicator::kSegments - 1), /*viewCount*/ 1, &shape2DShader, renderState)) { return false; }
  if (!Text2D::Create(&bufferingIndicator.text, /*viewCount*/ 1, fontstash.get(), renderState)) { return false; }
  
  return true;
}

void FlatscreenUI::Deinitialize() {
  videoTitle.reset();
  // betaTag.reset();
  
  helpTextRotate.reset();
  helpTextMove.reset();
  helpTextZoom.reset();
  helpTextRotateFinger.reset();
  helpTextMoveFinger.reset();
  helpTextZoomFinger.reset();
  helpTextRotateSeparator.reset();
  helpTextMoveSeparator.reset();
  helpTextZoomSeparator.reset();
  helpTextRotateMouse.reset();
  helpTextMoveMouse.reset();
  helpTextZoomMouse.reset();
  
  infoOverlayUI.Destroy();
  basicVideoControlsUI.Destroy();
  
  overlayBackgroundDimmer.reset();
  
  bufferingIndicator.shape.reset();
  bufferingIndicator.text.reset();
  
  shape2DShader.Destroy();
  
  fontstashShader.Destroy();
  fontstash.reset();
}

void FlatscreenUI::OnBackClicked(const function<void ()>& callback) {
  backClickedCallback = callback;
}

void FlatscreenUI::Update(
    s64 nanosecondsSinceLastFrame, float playbackRatio,
    bool showRepeatButton, bool showInfoButton, bool showBackButton, bool showBufferingIndicator, float bufferingPercent,
    int width, int height, float xdpi, float ydpi, RenderState* renderState) {
  this->width = width;
  this->height = height;
  repeatButtonShown = showRepeatButton;
  
  // Re-layout the UI
  auto cmToPixelsX = [&](float cm) {
    const float inches = 0.393701f * cm;
    return inches * xdpi;
  };
  auto cmToPixelsY = [&](float cm) {
    const float inches = 0.393701f * cm;
    return inches * ydpi;
  };
  
  constexpr float smallButtonSizeInCM = 1.0f;
  const Eigen::Vector2f smallButtonSize(
      static_cast<int>(cmToPixelsX(smallButtonSizeInCM) + 0.5f),
      static_cast<int>(cmToPixelsY(smallButtonSizeInCM) + 0.5f));
  
  constexpr float timelineHeightInCM = 0.1f;
  const int timelineHeight = max<int>(2, cmToPixelsY(timelineHeightInCM) + 0.5f);
  
  basicVideoControlsUI.SetGeometry(
      /*showPauseResumeRepeatButton*/ !infoOverlayShown, /*showTimeline*/ !infoOverlayShown, showInfoButton, showBackButton,
      isPaused, showRepeatButton, playbackRatio,
      /*minX*/ 0, /*maxX*/ width, /*bottomY*/ height, smallButtonSize, timelineHeight,
      renderState);
  
  // Texts.
  // If the screen width is too small, we scale everything down since our layout does not fit otherwise.
  fontstash->PrepareFrame(renderState);
  
  constexpr float minScreenWidthForFullLayoutInCM = 18;
  const float textScaleFactor = min(1.f, width / cmToPixelsX(minScreenWidthForFullLayoutInCM));
  
  const float textStartX = cmToPixelsX(textScaleFactor * 0.2f);
  const float textStartY = cmToPixelsY(textScaleFactor * 0.2f);
  
  const float titleFontSize = cmToPixelsY(textScaleFactor * 1.5f);
  videoTitle->SetText(textStartX, 0.f, FONS_ALIGN_LEFT | FONS_ALIGN_TOP, videoTitleText.c_str(), Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, titleFontSize, renderState);
  
  // const float betaTagFontSize = cmToPixelsY(textScaleFactor * 0.75f);
  // betaTag->SetText(textStartX, titleFontSize, FONS_ALIGN_LEFT | FONS_ALIGN_TOP, "[beta]", Eigen::Vector4f(0.996f, 0.464f, 0.027f, 1.f), fontstashFont, betaTagFontSize, renderState);
  
  const float helpTextFontSize = cmToPixelsY(textScaleFactor * 0.5f);
  
  // Help texts
  if (!infoOverlayShown) {
    float textX = width - textStartX;
    if (showTouchControlsHelp || showMouseControlsHelp) {
      helpTextRotate->SetText(textX, textStartY, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Rotate", Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextMove->SetText(textX, textStartY + helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Move", Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextZoom->SetText(textX, textStartY + 2 * helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Zoom", Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
      float xMin, yMin, xMax, yMax;
      helpTextRotate->GetTextBounds(textX, textStartY, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Rotate", fontstashFont, helpTextFontSize, &xMin, &yMin, &xMax, &yMax);
      const float rotateTextWidth = xMax - xMin;
      textX -= rotateTextWidth + 2 * textStartX;
    }
    
    if (showTouchControlsHelp) {
      float xMin, yMin, xMax, yMax;
      helpTextZoomFinger->GetTextBounds(textX, textStartY + 2 * helpTextFontSize, FONS_ALIGN_CENTER | FONS_ALIGN_TOP, "two-finger pinch", fontstashFont, helpTextFontSize, &xMin, &yMin, &xMax, &yMax);
      const float twoFingerPinchTextWidth = xMax - xMin;
      
      int hAlign = showMouseControlsHelp ? FONS_ALIGN_LEFT : FONS_ALIGN_RIGHT;
      if (hAlign == FONS_ALIGN_LEFT) {
        textX -= twoFingerPinchTextWidth;
      }
      helpTextRotateFinger->SetText(textX, textStartY, hAlign | FONS_ALIGN_TOP, "one-finger drag", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextMoveFinger->SetText(textX, textStartY + helpTextFontSize, hAlign | FONS_ALIGN_TOP, "two-finger drag", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextZoomFinger->SetText(textX, textStartY + 2 * helpTextFontSize, hAlign | FONS_ALIGN_TOP, "two-finger pinch", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      if (hAlign != FONS_ALIGN_LEFT) {
        textX -= twoFingerPinchTextWidth;
      }
    }
    
    if (showMouseControlsHelp && showTouchControlsHelp) {
      textX -= textStartX;
      helpTextRotateSeparator->SetText(textX, textStartY, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "|", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextMoveSeparator->SetText(textX, textStartY + helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "|", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextZoomSeparator->SetText(textX, textStartY + 2 * helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "|", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      float xMin, yMin, xMax, yMax;
      helpTextRotateSeparator->GetTextBounds(textX, textStartY, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "|", fontstashFont, helpTextFontSize, &xMin, &yMin, &xMax, &yMax);
      const float separatorTextWidth = xMax - xMin;
      textX -= separatorTextWidth + textStartX;
    }
    
    if (showMouseControlsHelp) {
      helpTextRotateMouse->SetText(textX, textStartY, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Left mouse drag", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextMoveMouse->SetText(textX, textStartY + helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Middle or right mouse drag", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
      helpTextZoomMouse->SetText(textX, textStartY + 2 * helpTextFontSize, FONS_ALIGN_RIGHT | FONS_ALIGN_TOP, "Mouse wheel", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
    }
  }
  
  // About overlay
  if (infoOverlayShown) {
    infoOverlayUI.SetGeometry(/*minX*/ 0, /*minY*/ 0, /*maxX*/ width, /*maxY*/ height, smallButtonSize, titleFontSize, helpTextFontSize, renderState);
  }
  
  // Buffering indicator
  bufferingIndicator.show = showBufferingIndicator;
  if (showBufferingIndicator) {
    constexpr float bufferingIndicatorRotationSpeed = 4.0f;
    constexpr float kAngleRange = static_cast<float>(0.7f * (2 * M_PI));
    
    constexpr float kInnerRadiusInCM = 0.5f;
    constexpr float kOuterRadiusInCM = 0.7f;
    
    const float innerRadius = cmToPixelsX(kInnerRadiusInCM);
    const float outerRadius = cmToPixelsX(kOuterRadiusInCM);
    
    bufferingIndicator.angle = fmodf((bufferingIndicator.angle - NanosecondsToSeconds(nanosecondsSinceLastFrame) * bufferingIndicatorRotationSpeed), 2 * M_PI);
    
    array<Eigen::Vector2f, 2 * BufferingIndicator::kSegments> bufferingIndicatorVertices;
    array<u16, 2 * 3 * (BufferingIndicator::kSegments - 1)> bufferingIndicatorIndices;
    
    const Eigen::Vector2f center(0.5f * width, 0.5f * height);
    
    for (int segment = 0; segment < BufferingIndicator::kSegments; ++ segment) {
      const float angle = bufferingIndicator.angle + kAngleRange * (segment / (BufferingIndicator::kSegments - 1.f));
      const Eigen::Vector2f direction(sinf(angle), cosf(angle));
      
      const int baseVertex = 2 * segment;
      bufferingIndicatorVertices[baseVertex + 0] = center + innerRadius * direction;
      bufferingIndicatorVertices[baseVertex + 1] = center + outerRadius * direction;
      
      if (segment < BufferingIndicator::kSegments - 1) {
        const int baseIndex = 2 * 3 * segment;
        bufferingIndicatorIndices[baseIndex + 0] = baseVertex + 0;
        bufferingIndicatorIndices[baseIndex + 1] = baseVertex + 1;
        bufferingIndicatorIndices[baseIndex + 2] = baseVertex + 2;
        
        bufferingIndicatorIndices[baseIndex + 3] = baseVertex + 2;
        bufferingIndicatorIndices[baseIndex + 4] = baseVertex + 1;
        bufferingIndicatorIndices[baseIndex + 5] = baseVertex + 3;
      }
    }
    
    bufferingIndicator.shape->SetGeometry(
        bufferingIndicatorVertices.size(), bufferingIndicatorVertices.data(),
        bufferingIndicatorIndices.size(), bufferingIndicatorIndices.data(),
        renderState);
    
    ostringstream bufferingText;
    bufferingText << "Buffering (" << static_cast<int>(bufferingPercent + 0.5f) << "%) ...";
    bufferingIndicator.text->SetText(
        0.5f * width, 0.5f * height + 1.3f * outerRadius + 0.5f * helpTextFontSize, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE,
        bufferingText.str().c_str(), Eigen::Vector4f(0.9f, 0.9f, 0.9f, 1.f), fontstashFont, helpTextFontSize, renderState);
  }
  
  // Overlay background dimmer
  const Eigen::Vector2f overlayBackgroundDimmerVertices[4] = {
      Eigen::Vector2f(0, 0),
      Eigen::Vector2f(width, 0),
      Eigen::Vector2f(0, height),
      Eigen::Vector2f(width, height)};
  
  const u16 overlayBackgroundDimmerIndices[3 * 2] = {
        0, 2, 1,
        1, 2, 3};
  
  overlayBackgroundDimmer->SetGeometry(/*vertexCount*/ 4, overlayBackgroundDimmerVertices, /*indexCount*/ 6, overlayBackgroundDimmerIndices, renderState);
}

void FlatscreenUI::RenderView(RenderState* renderState) {
  Eigen::Matrix4f currentModelViewProjection = Eigen::Matrix4f::Identity();
  currentModelViewProjection(0, 0) = 2. / width;
  currentModelViewProjection(0, 3) = -1;
  currentModelViewProjection(1, 1) = 2. / height;
  currentModelViewProjection(1, 3) = -1;
  
  // Currently, the Vulkan implementation *requires* to set the matrix after calling RenderView(),
  // since SetModelViewProjection() accesses currentFrameInFlightIndex, which is set by RenderView().
  // In contrast, for OpenGL, we must call SetModelViewProjection() first,
  // since RenderView() accesses the matrix set by it.
  if (renderState->api != RenderState::RenderingAPI::Vulkan) {
    SetModelViewProjection(currentModelViewProjection);
  }
  
  const Eigen::Vector4f inactiveButtonColor(0.8f, 0.8f, 0.8f, 1.f);
  const Eigen::Vector4f activeButtonColor(0.9f, 0.9f, 0.9f, 1.f);
  
  if (infoOverlayShown || bufferingIndicator.show) {
    overlayBackgroundDimmer->RenderView(Eigen::Vector4f(0.f, 0.f, 0.f, 0.7f), /*viewIndex*/ 0, &shape2DShader, renderState);
  }
  
  basicVideoControlsUI.RenderView(/*viewIndex*/ 0, /*distinctHoverColors*/ false, renderState);
  
  if (bufferingIndicator.show && !infoOverlayShown) {
    bufferingIndicator.shape->RenderView(activeButtonColor, /*viewIndex*/ 0, &shape2DShader, renderState);
    bufferingIndicator.text->RenderView(/*viewIndex*/ 0, renderState);
  }
  
  if (infoOverlayShown) {
    infoOverlayUI.RenderView(/*viewIndex*/ 0, /*distinctHoverColors*/ false, renderState);
  } else {
    videoTitle->RenderView(/*viewIndex*/ 0, renderState);
    // betaTag->RenderView(/*viewIndex*/ 0, renderState);
    helpTextRotate->RenderView(/*viewIndex*/ 0, renderState);
    helpTextMove->RenderView(/*viewIndex*/ 0, renderState);
    helpTextZoom->RenderView(/*viewIndex*/ 0, renderState);
    
    helpTextRotateFinger->RenderView(/*viewIndex*/ 0, renderState);
    helpTextMoveFinger->RenderView(/*viewIndex*/ 0, renderState);
    helpTextZoomFinger->RenderView(/*viewIndex*/ 0, renderState);
    
    helpTextRotateSeparator->RenderView(/*viewIndex*/ 0, renderState);
    helpTextMoveSeparator->RenderView(/*viewIndex*/ 0, renderState);
    helpTextZoomSeparator->RenderView(/*viewIndex*/ 0, renderState);
    
    helpTextRotateMouse->RenderView(/*viewIndex*/ 0, renderState);
    helpTextMoveMouse->RenderView(/*viewIndex*/ 0, renderState);
    helpTextZoomMouse->RenderView(/*viewIndex*/ 0, renderState);
  }
  
  // To debug the FontStash texture, uncomment:
  // FONSText debug;
  // fonsDrawDebug(fontstash->GetContext(), 100, 50, &debug);
  // fonsBatchText(fontstash->GetContext(), debug);
  
  if (renderState->api == RenderState::RenderingAPI::Vulkan) {
    SetModelViewProjection(currentModelViewProjection);
  }
}

bool FlatscreenUI::Hover(float x, float y) {
  basicVideoControlsUI.ResetHoverStates();
  infoOverlayUI.ResetHoverStates();
  
  bool anythingHovered = basicVideoControlsUI.AddHover(x, y);
  if (infoOverlayShown) {
    anythingHovered |= infoOverlayUI.AddHover(x, y);
  }
  
  return anythingHovered;
}

bool FlatscreenUI::TouchOrClickDown(float x, float y) {
  const Eigen::Vector2f pos(x, y);
  float factor;
  
  if (basicVideoControlsUI.PauseResumeRepeatButtonClicked(x, y) && xrVideo->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
    if (repeatButtonShown) {
      xrVideo->Seek(xrVideo->Index().GetVideoStartTimestamp(), /*forwardPlayback*/ true);
      if (audio) { audio->SetPlaybackPosition(0, /*forward*/ true); }
      isPaused = false;
    } else {
      isPaused = !isPaused;
    }
  } else if (basicVideoControlsUI.InfoButtonClicked(x, y)) {
    infoOverlayShown = !infoOverlayShown;
  } else if (basicVideoControlsUI.BackButtonClicked(x, y)) {
    if (backClickedCallback) { backClickedCallback(); }
  } else if (basicVideoControlsUI.TimelineClicked(x, y, &factor) && xrVideo->GetAsyncLoadState() == XRVideoAsyncLoadState::Ready) {
    const s64 videoStartTimestamp = xrVideo->Index().GetVideoStartTimestamp();
    const s64 videoEndTimestamp = xrVideo->Index().GetVideoEndTimestamp();
    const s64 timestamp = max(videoStartTimestamp, min<s64>(videoEndTimestamp, round(videoStartTimestamp + (1.0 * factor) * (videoEndTimestamp - videoStartTimestamp))));
    
    auto& playbackState = xrVideo->GetPlaybackState();
    playbackState.Lock();
    const bool forwardPlayback = playbackState.PlayingForward();
    playbackState.Unlock();
    
    xrVideo->Seek(timestamp, forwardPlayback);
    if (audio) { audio->SetPlaybackPosition(timestamp - videoStartTimestamp, forwardPlayback); }
  } else if (infoOverlayShown && infoOverlayUI.TouchOrClickDown(/*cursorId*/ 0, x, y)){
    return true;
  } else {
    return false;
  }
  
  return true;
}

bool FlatscreenUI::TouchOrClickMove(float x, float y) {
  return infoOverlayUI.TouchOrClickMove(/*cursorId*/ 0, x, y);
}

bool FlatscreenUI::TouchOrClickUp(float x, float y) {
  return infoOverlayUI.TouchOrClickUp(/*cursorId*/ 0, x, y);
}

bool FlatscreenUI::WheelRotated(float degrees) {
  if (infoOverlayShown) {
    infoOverlayUI.WheelRotated(degrees);
    return true;
  }
  
  return false;
}

void FlatscreenUI::SetModelViewProjection(const Eigen::Matrix4f& matrix) {
  if (infoOverlayShown || bufferingIndicator.show) {
    overlayBackgroundDimmer->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
  }
  
  basicVideoControlsUI.SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
  
  if (bufferingIndicator.show && !infoOverlayShown) {
    bufferingIndicator.shape->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    bufferingIndicator.text->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
  }
  
  if (infoOverlayShown) {
    infoOverlayUI.SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
  } else {
    videoTitle->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    // betaTag->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextRotate->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextMove->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextZoom->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    
    helpTextRotateFinger->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextMoveFinger->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextZoomFinger->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    
    helpTextRotateSeparator->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextMoveSeparator->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextZoomSeparator->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    
    helpTextRotateMouse->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextMoveMouse->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
    helpTextZoomMouse->SetModelViewProjection(/*viewIndex*/ 0, matrix.data());
  }
}

}
