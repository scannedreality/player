#include "scan_studio/viewer_common/ui_common.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

#include <loguru.hpp>

#include "scan_studio/viewer_common/license_texts.hpp"
#include "scan_studio/viewer_common/pi.hpp"
#include "scan_studio/viewer_common/ui_icons.hpp"

namespace scan_studio {

bool BasicVideoControlsUI::Initialize(int viewCount, Shape2DShader* shape2DShader, RenderState* renderState) {
  this->shape2DShader = shape2DShader;
  
  const auto& repeatIcon = RepeatIcon::Instance();
  
  if (!Shape2D::Create(&pauseResumeRepeatButton, /*maxVertices*/ max<int>(8, repeatIcon.vertices.size()), /*maxIndices*/ max<int>(12, repeatIcon.indices.size()), viewCount, shape2DShader, renderState)) { return false; }
  if (!Shape2D::Create(&timeline, /*maxVertices*/ 4, /*maxIndices*/ 6, viewCount, shape2DShader, renderState)) { return false; }
  if (!Shape2D::Create(&timelineMarkedRange, /*maxVertices*/ 4, /*maxIndices*/ 6, viewCount, shape2DShader, renderState)) { return false; }
  if (!Shape2D::Create(&infoButton, /*maxVertices*/ 12, /*maxIndices*/ 3 * 10, viewCount, shape2DShader, renderState)) { return false; }
  if (!Shape2D::Create(&backButton, /*maxVertices*/ 9, /*maxIndices*/ 3 * 7, viewCount, shape2DShader, renderState)) { return false; }
  
  return true;
}

void BasicVideoControlsUI::Destroy() {
  pauseResumeRepeatButton.reset();
  timeline.reset();
  timelineMarkedRange.reset();
  infoButton.reset();
  backButton.reset();
}

template <usize vertexCount, usize indexCount>
static void CreateScaledIconShape(
    const Eigen::Vector2f& min,
    const Eigen::Vector2f& size,
    const array<Eigen::Vector2f, vertexCount>& vertices,
    const array<u16, indexCount>& indices,
    Shape2D* shape,
    RenderState* renderState) {
  array<Eigen::Vector2f, vertexCount> scaledVertices;
  
  for (int i = 0; i < scaledVertices.size(); ++ i) {
    scaledVertices[i] = min + vertices[i].cwiseProduct(size);
  }
  
  shape->SetGeometry(scaledVertices.size(), scaledVertices.data(), indices.size(), indices.data(), renderState);
};

void BasicVideoControlsUI::SetGeometry(
    bool showPauseResumeRepeatButton, bool showTimeline, bool showInfoButton, bool showBackButton,
    bool isPaused, bool showRepeatButton, float playbackRatio,
    float minX, float maxX, float bottomY, const Eigen::Vector2f& smallButtonSize, float timelineHeight,
    RenderState* renderState) {
  this->showPauseResumeRepeatButton = showPauseResumeRepeatButton;
  this->showTimeline = showTimeline;
  this->showInfoButton = showInfoButton;
  this->showBackButton = showBackButton;
  
  // Pause-Resume-Repeat button
  if (showPauseResumeRepeatButton) {
    pauseResumeRepeatButtonBox = Eigen::AlignedBox<float, 2>(
        Eigen::Vector2f(minX, bottomY - smallButtonSize.y()),
        Eigen::Vector2f(minX + smallButtonSize.x(), bottomY));
    
    if (showRepeatButton) {
      const auto& repeatIcon = RepeatIcon::Instance();
      CreateScaledIconShape(pauseResumeRepeatButtonBox.min(), smallButtonSize, repeatIcon.vertices, repeatIcon.indices, pauseResumeRepeatButton.get(), renderState);
    } else if (!isPaused) {
      CreateScaledIconShape(pauseResumeRepeatButtonBox.min(), smallButtonSize, pauseIconVertices, pauseIconIndices, pauseResumeRepeatButton.get(), renderState);
    } else {
      CreateScaledIconShape(pauseResumeRepeatButtonBox.min(), smallButtonSize, resumeIconVertices, resumeIconIndices, pauseResumeRepeatButton.get(), renderState);
    }
  }
  
  // Info button
  if (showInfoButton) {
    infoButtonBox = Eigen::AlignedBox<float, 2>(
        Eigen::Vector2f(maxX - (showBackButton ? 2 : 1) * smallButtonSize.x(), bottomY - smallButtonSize.y()),
        Eigen::Vector2f(maxX, bottomY));
    CreateScaledIconShape(infoButtonBox.min(), smallButtonSize, infoIconVertices, infoIconIndices, infoButton.get(), renderState);
  }
  
  // Back button
  if (showBackButton) {
    backButtonBox = Eigen::AlignedBox<float, 2>(
        Eigen::Vector2f(maxX - smallButtonSize.x(), bottomY - smallButtonSize.y()),
        Eigen::Vector2f(maxX, bottomY));
    CreateScaledIconShape(backButtonBox.min(), smallButtonSize, backIconVertices, backIconIndices, backButton.get(), renderState);
  }
  
  // Timeline
  if (showTimeline) {
    const float rightMargin = std::max<float>(0.2f, ((showInfoButton ? 1 : 0) + (showBackButton ? 1 : 0))) * smallButtonSize.x();
    
    timelineInteractiveAreaBox = Eigen::AlignedBox<float, 2>(
        Eigen::Vector2f(minX + smallButtonSize.x(), bottomY - smallButtonSize.y()),
        Eigen::Vector2f(maxX - rightMargin, bottomY));
    
    const Eigen::Vector2f timelineMin(Eigen::Vector2f(minX + smallButtonSize.x(), bottomY - 0.5f * smallButtonSize.y() - 0.5f * timelineHeight));
    const Eigen::Vector2f timelineMax(Eigen::Vector2f(maxX - rightMargin, bottomY - 0.5f * smallButtonSize.y() + 0.5f * timelineHeight));
    
    const float markerX = timelineMin.x() + playbackRatio * (timelineMax.x() - timelineMin.x());
    
    array<Eigen::Vector2f, 4> timelineVertices;
    const array<u16, 6> timelineIndices = {0, 3, 1, 1, 3, 2};
    
    timelineVertices[0] = Eigen::Vector2f(markerX, timelineMin.y());
    timelineVertices[1] = Eigen::Vector2f(timelineMax.x(), timelineMin.y());
    timelineVertices[2] = timelineMax;
    timelineVertices[3] = Eigen::Vector2f(markerX, timelineMax.y());
    timeline->SetGeometry(timelineVertices.size(), timelineVertices.data(), timelineIndices.size(), timelineIndices.data(), renderState);
    
    // Timeline marked range
    timelineVertices[0] = timelineMin;
    timelineVertices[1] = Eigen::Vector2f(markerX, timelineMin.y());
    timelineVertices[2] = Eigen::Vector2f(markerX, timelineMax.y());
    timelineVertices[3] = Eigen::Vector2f(timelineMin.x(), timelineMax.y());
    timelineMarkedRange->SetGeometry(timelineVertices.size(), timelineVertices.data(), timelineIndices.size(), timelineIndices.data(), renderState);
  }
}

void BasicVideoControlsUI::ResetHoverStates() {
  pauseResumeButtonHovered = false;
  timelineHovered = false;
  infoButtonHovered = false;
  backButtonHovered = false;
}

bool BasicVideoControlsUI::AddHover(float x, float y) {
  const Eigen::Vector2f pos(x, y);
  
  const bool hoverA = showPauseResumeRepeatButton && pauseResumeRepeatButtonBox.contains(pos);
  pauseResumeButtonHovered |= hoverA;
  
  const bool hoverB = showTimeline && timelineInteractiveAreaBox.contains(pos);
  timelineHovered |= hoverB;
  
  const bool hoverC = showInfoButton && infoButtonBox.contains(pos);
  infoButtonHovered |= hoverC;
  
  const bool hoverD = showBackButton && backButtonBox.contains(pos);
  backButtonHovered |= hoverD;
  
  return hoverA ||
         hoverB ||
         hoverC ||
         hoverD;
}

bool BasicVideoControlsUI::PauseResumeRepeatButtonClicked(float x, float y) {
  return showPauseResumeRepeatButton && pauseResumeRepeatButtonBox.contains(Eigen::Vector2f(x, y));
}

bool BasicVideoControlsUI::TimelineClicked(float x, float y, float* factor) {
  if (showTimeline && timelineInteractiveAreaBox.contains(Eigen::Vector2f(x, y))) {
    *factor = (x - timelineInteractiveAreaBox.min().x()) / (timelineInteractiveAreaBox.max().x() - timelineInteractiveAreaBox.min().x());
    return true;
  }
  return false;
}

bool BasicVideoControlsUI::InfoButtonClicked(float x, float y) {
  return showInfoButton && infoButtonBox.contains(Eigen::Vector2f(x, y));
}

bool BasicVideoControlsUI::BackButtonClicked(float x, float y) {
  return showBackButton && backButtonBox.contains(Eigen::Vector2f(x, y));
}

void BasicVideoControlsUI::RenderView(int viewIndex, bool distinctHoverColors, RenderState* renderState) {
  const Eigen::Vector4f inactiveButtonColor(0.8f, 0.8f, 0.8f, 1.f);
  const Eigen::Vector4f activeButtonColor = distinctHoverColors ? Eigen::Vector4f(1.0f, 0.7f, 0.25f, 1.f) : Eigen::Vector4f(0.9f, 0.9f, 0.9f, 1.f);
  
  const Eigen::Vector4f inactiveTimelineColor(0.8f, 0.8f, 0.8f, 1.f);
  const Eigen::Vector4f activeTimelineColor = distinctHoverColors ? Eigen::Vector4f(1.0f, 1.0f, 1.0f, 1.f) : Eigen::Vector4f(0.9f, 0.9f, 0.9f, 1.f);
  
  const Eigen::Vector4f inactiveTimelineMarkerColor(0.9f, 0.2f, 0.2f, 1.f);
  const Eigen::Vector4f activeTimelineMarkerColor = distinctHoverColors ? Eigen::Vector4f(1.0f, 0.0f, 0.0f, 1.f) : Eigen::Vector4f(1.0f, 0.3f, 0.3f, 1.f);
  
  if (showPauseResumeRepeatButton) {
    pauseResumeRepeatButton->RenderView(pauseResumeButtonHovered ? activeButtonColor : inactiveButtonColor, viewIndex, shape2DShader, renderState);
  }
  
  if (showTimeline) {
    timeline->RenderView(timelineHovered ? activeTimelineColor : inactiveTimelineColor, viewIndex, shape2DShader, renderState);
    timelineMarkedRange->RenderView(timelineHovered ? activeTimelineMarkerColor : inactiveTimelineMarkerColor, viewIndex, shape2DShader, renderState);
  }
  
  if (showInfoButton) {
    infoButton->RenderView(infoButtonHovered ? activeButtonColor : inactiveButtonColor, viewIndex, shape2DShader, renderState);
  }
  
  if (showBackButton) {
    backButton->RenderView(backButtonHovered ? activeButtonColor : inactiveButtonColor, viewIndex, shape2DShader, renderState);
  }
}

void BasicVideoControlsUI::SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) {
  if (showPauseResumeRepeatButton) {
    pauseResumeRepeatButton->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
  
  if (showTimeline) {
    timeline->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
    timelineMarkedRange->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
  
  if (showInfoButton) {
    infoButton->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
  
  if (showBackButton) {
    backButton->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
}


bool InfoOverlayUI::Initialize(bool showLegalNoticeAndPrivacyLinks, int viewCount, Shape2DShader* shape2DShader, FontStash* fontstash, int fontstashFont, float fontScaling, RenderState* renderState) {
  this->showLegalNoticeAndPrivacyLinks = showLegalNoticeAndPrivacyLinks;
  
  this->fontstash = fontstash;
  this->fontstashFont = fontstashFont;
  this->fontScaling = fontScaling;
  this->shape2DShader = shape2DShader;
  
  if (!Text2D::Create(&aboutTitle, viewCount, fontstash, renderState)) { return false; }
  if (!Text2D::Create(&aboutDescription, viewCount, fontstash, renderState)) { return false; }
  if (showLegalNoticeAndPrivacyLinks) {
    if (!Text2D::Create(&aboutLegalNotice, viewCount, fontstash, renderState)) { return false; }
    if (!Text2D::Create(&aboutPrivacy, viewCount, fontstash, renderState)) { return false; }
  }
  if (!Text2D::Create(&aboutOpenSourceLicenses, viewCount, fontstash, renderState)) { return false; }
  if (!Text2D::Create(&aboutOpenSourceLicenses2, viewCount, fontstash, renderState)) { return false; }
  if (!Text2D::Create(&aboutOpenSourceComponent, viewCount, fontstash, renderState)) { return false; }
  if (!Shape2D::Create(&nextOpenSourceComponentArrow, /*maxVertices*/ 3, /*maxIndices*/ 3, viewCount, shape2DShader, renderState)) { return false; }
  if (!Shape2D::Create(&prevOpenSourceComponentArrow, /*maxVertices*/ 3, /*maxIndices*/ 3, viewCount, shape2DShader, renderState)) { return false; }
  
  constexpr int maxLicenseTextLinesShown = 64;
  licenseTextLines.resize(maxLicenseTextLinesShown);
  licenseTextFadeStates.resize(maxLicenseTextLinesShown);
  for (int i = 0; i < licenseTextLines.size(); ++ i) {
    if (!Text2D::Create(&licenseTextLines[i], viewCount, fontstash, renderState)) { return false; }
  }
  
  return true;
}

void InfoOverlayUI::Destroy() {
  aboutTitle.reset();
  aboutDescription.reset();
  aboutLegalNotice.reset();
  aboutPrivacy.reset();
  aboutOpenSourceLicenses.reset();
  aboutOpenSourceLicenses2.reset();
  aboutOpenSourceComponent.reset();
  nextOpenSourceComponentArrow.reset();
  prevOpenSourceComponentArrow.reset();
  
  licenseTextLines.clear();
  licenseTextFadeStates.clear();
}

void InfoOverlayUI::SetGeometry(float minX, float minY, float maxX, float maxY, const Eigen::Vector2f& smallButtonSize, float titleFontSize, float helpTextFontSize, RenderState* renderState) {
  const float centerX = 0.5f * (minX + maxX);
  const float height = maxY - minY;
  
  const float scaledTitleFontSize = fontScaling * titleFontSize;
  const float scaledHelpTextFontSize = fontScaling * helpTextFontSize;
  
  aboutTitle->SetText(centerX, minY + 0.1f * height, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE, "About", Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, titleFontSize, renderState);
  aboutDescription->SetText(centerX, minY + 0.1f * height + 0.55f * scaledTitleFontSize, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE, "Volumetric video player by ScannedReality", Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
  if (showLegalNoticeAndPrivacyLinks) {
    const float legalNoticeX = centerX - scaledHelpTextFontSize;
    const float legalNoticeY = minY + 0.1f * height + 0.55f * scaledTitleFontSize + scaledHelpTextFontSize;
    const int legalNoticeAlign = FONS_ALIGN_RIGHT | FONS_ALIGN_MIDDLE;
    const char* legalNoticeText = "Legal notice/Impressum";
    aboutLegalNotice->SetText(legalNoticeX, legalNoticeY, legalNoticeAlign, legalNoticeText, Eigen::Vector4f(0.2f, 0.2f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
    aboutLegalNotice->GetTextBounds(legalNoticeX, legalNoticeY, legalNoticeAlign, legalNoticeText, fontstashFont, helpTextFontSize, &legalNoticeBox.min().x(), &legalNoticeBox.min().y(), &legalNoticeBox.max().x(), &legalNoticeBox.max().y());
    
    const float privacyX = centerX + scaledHelpTextFontSize;
    const float privacyY = legalNoticeY;
    const int privacyAlign = FONS_ALIGN_LEFT | FONS_ALIGN_MIDDLE;
    const char* privacyText = "Privacy/Datenschutz";
    aboutPrivacy->SetText(privacyX, privacyY, privacyAlign, privacyText, Eigen::Vector4f(0.2f, 0.2f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
    aboutPrivacy->GetTextBounds(privacyX, privacyY, privacyAlign, privacyText, fontstashFont, helpTextFontSize, &privacyBox.min().x(), &privacyBox.min().y(), &privacyBox.max().x(), &privacyBox.max().y());
  }
  aboutOpenSourceLicenses->SetText(centerX, minY + 0.25f * height - 1.5f * scaledHelpTextFontSize, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE, "Open-source license statements are reproduced below.", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
  aboutOpenSourceLicenses2->SetText(centerX, minY + 0.25f * height - 0.5f * scaledHelpTextFontSize, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE, "Scroll by dragging or with the mouse wheel.", Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f), fontstashFont, helpTextFontSize, renderState);
  
  const float componentSelectorY = minY + 0.25f * height + 3.0f * scaledHelpTextFontSize;
  const string& componentName = openSourceComponents[selectedOpenSourceComponent].name;
  const int componentAlign = FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE;
  aboutOpenSourceComponent->SetText(centerX, componentSelectorY, componentAlign, componentName.c_str(), Eigen::Vector4f(1.f, 1.f, 1.f, 1.f), fontstashFont, helpTextFontSize, renderState);
  float xMin, yMin, xMax, yMax;
  aboutOpenSourceComponent->GetTextBounds(centerX, componentSelectorY, componentAlign, componentName.c_str(), fontstashFont, helpTextFontSize, &xMin, &yMin, &xMax, &yMax);
  const float openSourceComponentWidth = xMax - xMin;
  
  // Open-source component toggle arrows:
  // Next
  nextOpenSourceComponentArrowBox = Eigen::AlignedBox<float, 2>(
      Eigen::Vector2f(centerX + 0.5f * openSourceComponentWidth + 0.5f * smallButtonSize.x(), componentSelectorY - 0.5f * smallButtonSize.y()),
      Eigen::Vector2f(centerX + 0.5f * openSourceComponentWidth + 1.5f * smallButtonSize.x(), componentSelectorY + 0.5f * smallButtonSize.y()));
  
  Eigen::Vector2f nextPrevButtonVertices[3];
  nextPrevButtonVertices[0] = nextOpenSourceComponentArrowBox.min() + Eigen::Vector2f(0.241f, 0.168f).cwiseProduct(smallButtonSize);
  nextPrevButtonVertices[1] = nextOpenSourceComponentArrowBox.min() + Eigen::Vector2f(0.241f, 0.832f).cwiseProduct(smallButtonSize);
  nextPrevButtonVertices[2] = nextOpenSourceComponentArrowBox.min() + Eigen::Vector2f(0.792f, 0.500f).cwiseProduct(smallButtonSize);
  const u16 nextPrevButtonIndices[3] = {0, 1, 2};
  nextOpenSourceComponentArrow->SetGeometry(/*vertexCount*/ 3, nextPrevButtonVertices, /*indexCount*/ 3, nextPrevButtonIndices, renderState);
  
  // Prev
  prevOpenSourceComponentArrowBox = Eigen::AlignedBox<float, 2>(
      Eigen::Vector2f(centerX - 0.5f * openSourceComponentWidth - 1.5f * smallButtonSize.x(), componentSelectorY - 0.5f * smallButtonSize.y()),
      Eigen::Vector2f(centerX - 0.5f * openSourceComponentWidth - 0.5f * smallButtonSize.x(), componentSelectorY + 0.5f * smallButtonSize.y()));
  
  Eigen::Vector2f prevPrevButtonVertices[3];
  prevPrevButtonVertices[0] = prevOpenSourceComponentArrowBox.min() + Eigen::Vector2f(1 - 0.241f, 0.168f).cwiseProduct(smallButtonSize);
  prevPrevButtonVertices[1] = prevOpenSourceComponentArrowBox.min() + Eigen::Vector2f(1 - 0.241f, 0.832f).cwiseProduct(smallButtonSize);
  prevPrevButtonVertices[2] = prevOpenSourceComponentArrowBox.min() + Eigen::Vector2f(1 - 0.792f, 0.500f).cwiseProduct(smallButtonSize);
  const u16 prevPrevButtonIndices[3] = {2, 1, 0};
  prevOpenSourceComponentArrow->SetGeometry(/*vertexCount*/ 3, prevPrevButtonVertices, /*indexCount*/ 3, prevPrevButtonIndices, renderState);
  
  // License text lines
  const vector<string>* licenseTextStringRows = openSourceComponents[selectedOpenSourceComponent].licenseLines;
  
  const float licenseTextTop = minY + 0.25f * height + 5.5f * scaledHelpTextFontSize;
  const float licenseTextBottom = minY + 0.9f * height;
  
  const float licenseTextFade = 1.f * scaledHelpTextFontSize;
  const float licenseTextHeight = licenseTextStringRows->size() * scaledHelpTextFontSize;
  
  licenseTextMaxScroll = max(0.f, licenseTextTop + licenseTextHeight - licenseTextBottom);
  
  licenseTextArea = Eigen::AlignedBox<float, 2>(
      Eigen::Vector2f(minX, licenseTextTop),
      Eigen::Vector2f(maxX, licenseTextBottom));
  licenseTextScrollStep = 3 * scaledHelpTextFontSize;
  
  const int firstRow = max(0, static_cast<int>(ceil((licenseTextScroll - licenseTextFade) / scaledHelpTextFontSize)));
  visibleLicenseTextLines = 0;
  
  for (int row = firstRow; row < licenseTextStringRows->size(); ++ row) {
    const int displayRow = row - firstRow;
    if (displayRow >= licenseTextLines.size()) {
      break;
    }
    
    const float textY = licenseTextTop + licenseTextFade - licenseTextScroll + row * scaledHelpTextFontSize;
    if (textY > licenseTextBottom) {
      break;
    }
    
    ++ visibleLicenseTextLines;
    
    if (textY < licenseTextTop + licenseTextFade) {
      licenseTextFadeStates[displayRow] = (textY - licenseTextTop) / licenseTextFade;
    } else if (textY > licenseTextBottom - licenseTextFade) {
      licenseTextFadeStates[displayRow] = (licenseTextBottom - textY) / licenseTextFade;
    } else {
      licenseTextFadeStates[displayRow] = 1.f;
    }
    
    const Eigen::Vector4f rowColorWithPremultipliedAlpha = Eigen::Vector4f(0.8f, 0.8f, 0.8f, 1.f) * licenseTextFadeStates[displayRow];
    licenseTextLines[displayRow]->SetText(centerX, textY, FONS_ALIGN_CENTER | FONS_ALIGN_MIDDLE, (*licenseTextStringRows)[row].c_str(), rowColorWithPremultipliedAlpha, fontstashFont, helpTextFontSize, renderState);
  }
}

void InfoOverlayUI::ResetHoverStates() {
  nextOpenSourceComponentArrowHovered = false;
  prevOpenSourceComponentArrowHovered = false;
}

bool InfoOverlayUI::AddHover(float x, float y) {
  const Eigen::Vector2f pos(x, y);
  
  const bool hoverA = nextOpenSourceComponentArrowBox.contains(pos);
  nextOpenSourceComponentArrowHovered |= hoverA;
  
  const bool hoverB = prevOpenSourceComponentArrowBox.contains(pos);
  prevOpenSourceComponentArrowHovered |= hoverB;
  
  bool hover = hoverA || hoverB;
  
  if (showLegalNoticeAndPrivacyLinks) {
    hover = hover || (!legalNoticeBox.isEmpty() && legalNoticeBox.contains(pos)) || (!privacyBox.isEmpty() && privacyBox.contains(pos));
  }
  
  return hover;
}

#ifdef __EMSCRIPTEN__
static void OpenLegalNoticePage() {
  EM_ASM(window.open('/legal_notice', '_blank'););
}

static void OpenPrivacyPage() {
  EM_ASM(window.open('/privacy_policy', '_blank'););
}
#endif

bool InfoOverlayUI::TouchOrClickDown(s64 cursorId, float x, float y) {
  Eigen::Vector2f pos(x, y);
  
  if (showLegalNoticeAndPrivacyLinks) {
    if (!legalNoticeBox.isEmpty() && legalNoticeBox.contains(pos)) {
      #ifdef __EMSCRIPTEN__
        emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_V, &OpenLegalNoticePage);
      #else
        LOG(ERROR) << "Link opening is not implemented for this environment";
      #endif
      return true;
    } else if (!privacyBox.isEmpty() && privacyBox.contains(pos)) {
      #ifdef __EMSCRIPTEN__
        emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_V, &OpenPrivacyPage);
      #else
        LOG(ERROR) << "Link opening is not implemented for this environment";
      #endif
      return true;
    }
  }
  
  if (!nextOpenSourceComponentArrowBox.isEmpty() && nextOpenSourceComponentArrowBox.contains(pos)) {
    selectedOpenSourceComponent = (selectedOpenSourceComponent + 1) % openSourceComponentCount;
    licenseTextScroll = 0;
  } else if (!prevOpenSourceComponentArrowBox.isEmpty() && prevOpenSourceComponentArrowBox.contains(pos)) {
    selectedOpenSourceComponent = (selectedOpenSourceComponent - 1 + openSourceComponentCount) % openSourceComponentCount;
    licenseTextScroll = 0;
  } else if (!licenseTextArea.isEmpty() && licenseTextArea.contains(pos)) {
    cursorLastDragPos[cursorId] = pos;
  } else {
    return false;
  }
  return true;
}

bool InfoOverlayUI::TouchOrClickMove(s64 cursorId, float x, float y) {
  const auto cursorLastDragPosIt = cursorLastDragPos.find(cursorId);
  if (cursorLastDragPosIt != cursorLastDragPos.end()) {
    const Eigen::Vector2f pos(x, y);
    
    licenseTextScroll -= pos.y() - cursorLastDragPosIt->second.y();
    licenseTextScroll = max(0.f, min(licenseTextScroll, licenseTextMaxScroll));
    
    cursorLastDragPosIt->second = pos;
    return true;
  }
  return false;
}

bool InfoOverlayUI::TouchOrClickUp(s64 cursorId, float /*x*/, float /*y*/) {
  const auto cursorLastDragPosIt = cursorLastDragPos.find(cursorId);
  if (cursorLastDragPosIt != cursorLastDragPos.end()) {
    cursorLastDragPos.erase(cursorLastDragPosIt);
    return true;
  }
  return false;
}

void InfoOverlayUI::WheelRotated(float degrees) {
  licenseTextScroll -= degrees * licenseTextScrollStep;
  licenseTextScroll = max(0.f, min(licenseTextScroll, licenseTextMaxScroll));
}

void InfoOverlayUI::RenderView(int viewIndex, bool distinctHoverColors, RenderState* renderState) {
  const Eigen::Vector4f inactiveButtonColor(0.8f, 0.8f, 0.8f, 1.f);
  const Eigen::Vector4f activeButtonColor = distinctHoverColors ? Eigen::Vector4f(1.0f, 0.7f, 0.25f, 1.f) : Eigen::Vector4f(0.9f, 0.9f, 0.9f, 1.f);
  
  aboutTitle->RenderView(viewIndex, renderState);
  aboutDescription->RenderView(viewIndex, renderState);
  if (showLegalNoticeAndPrivacyLinks) {
    aboutLegalNotice->RenderView(viewIndex, renderState);
    aboutPrivacy->RenderView(viewIndex, renderState);
  }
  aboutOpenSourceLicenses->RenderView(viewIndex, renderState);
  aboutOpenSourceLicenses2->RenderView(viewIndex, renderState);
  aboutOpenSourceComponent->RenderView(viewIndex, renderState);
  
  nextOpenSourceComponentArrow->RenderView(nextOpenSourceComponentArrowHovered ? activeButtonColor : inactiveButtonColor, viewIndex, shape2DShader, renderState);
  prevOpenSourceComponentArrow->RenderView(prevOpenSourceComponentArrowHovered ? activeButtonColor : inactiveButtonColor, viewIndex, shape2DShader, renderState);
  
  for (int displayRow = 0; displayRow < visibleLicenseTextLines; ++ displayRow) {
    licenseTextLines[displayRow]->RenderView(viewIndex, renderState);
  }
}

void InfoOverlayUI::SetModelViewProjection(int viewIndex, const float* columnMajorModelViewProjectionData) {
  aboutTitle->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  aboutDescription->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  if (showLegalNoticeAndPrivacyLinks) {
    aboutLegalNotice->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
    aboutPrivacy->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
  aboutOpenSourceLicenses->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  aboutOpenSourceLicenses2->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  aboutOpenSourceComponent->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  
  nextOpenSourceComponentArrow->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  prevOpenSourceComponentArrow->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  
  for (int displayRow = 0; displayRow < visibleLicenseTextLines; ++ displayRow) {
    licenseTextLines[displayRow]->SetModelViewProjection(viewIndex, columnMajorModelViewProjectionData);
  }
}

}
