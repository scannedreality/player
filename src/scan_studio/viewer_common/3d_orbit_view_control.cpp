#include "scan_studio/viewer_common/3d_orbit_view_control.hpp"

#include <Eigen/Geometry>

#include "scan_studio/viewer_common/pi.hpp"

namespace scan_studio {

Eigen::Vector3f OrbitViewParameters::ComputeEyePosition() const {
  return lookAt + ComputeEyeOffset();
}

Eigen::Vector3f OrbitViewParameters::ComputeEyeOffset() const {
  const float horizontalRadius = radius * cosf(pitch);
  return Eigen::Vector3f(
      horizontalRadius * sinf(yaw),
      radius * sinf(pitch),
      horizontalRadius * cosf(yaw));
}


void TouchOrbitViewController::Initialize(float xdpi, float ydpi, OrbitViewParameters* view) {
  this->xdpi = xdpi;
  this->ydpi = ydpi;
  this->view = view;
}

void TouchOrbitViewController::SingleFingerDrag(float oldX, float oldY, float newX, float newY) {
  if (!view) { return; }
  
  constexpr float dragSpeedFactor = 0.003f;
  
  view->yaw = view->yaw - (443.f / xdpi * dragSpeedFactor) * (newX - oldX);
  view->pitch = max<float>(-M_PI / 2 + 0.01f, min<float>(M_PI / 2 - 0.01f, view->pitch + (443.f / ydpi * dragSpeedFactor) * (newY - oldY)));
}

void TouchOrbitViewController::TwoFingerPinchOrDrag(float radiusRatio, float centerDiffX, float centerDiffY) {
  if (!view) { return; }
  
  // Pan the camera
  constexpr float panSpeedFactor = 0.001f;
  
  float horizontalRadius = view->radius * cosf(view->pitch);
  Eigen::Vector3f eyeDir(horizontalRadius * sinf(view->yaw), view->radius * sinf(view->pitch), horizontalRadius * cosf(view->yaw));
  Eigen::Vector3f up(0, 1, 0);
  Eigen::Vector3f right = up.cross(eyeDir).normalized();
  up = eyeDir.cross(right).normalized();
  
  view->lookAt += -(443.f / xdpi * panSpeedFactor) * centerDiffX * right +
                   (443.f / ydpi * panSpeedFactor) * centerDiffY * up;
  
  // Zoom the camera
  const float radiusFactor = 1.f / radiusRatio;
  if (!isnan(radiusFactor)) {
    view->radius = min(20.f, max(0.15f, radiusFactor * view->radius));
  }
}


void MouseOrbitViewController::Initialize(OrbitViewParameters* view) {
  this->view = view;
}

void MouseOrbitViewController::MouseDown(WindowCallbacks::MouseButton button, int x, int y) {
  if (!view) { return; }
  
  if (button == WindowCallbacks::MouseButton::kLeft) {
    leftMouseButtonPressed = true;
  } else if (button == WindowCallbacks::MouseButton::kMiddle) {
    middleMouseButtonPressed = true;
  } else if (button == WindowCallbacks::MouseButton::kRight) {
    rightMouseButtonPressed = true;
  }
  
  lastMouseX = x;
  lastMouseY = y;
}

void MouseOrbitViewController::MouseMove(int x, int y) {
  if (!view) { return; }
  
  if (leftMouseButtonPressed) {
    view->yaw = view->yaw - 0.01 * (x - lastMouseX);
    view->pitch = max(-M_PI / 2 + 0.01, min(M_PI / 2 - 0.01, view->pitch + 0.01 * (y - lastMouseY)));
    
    lastMouseX = x;
    lastMouseY = y;
  } else if (middleMouseButtonPressed || rightMouseButtonPressed) {
    Eigen::Vector3f eyeOffset = view->ComputeEyeOffset();
    Eigen::Vector3f up(0, 1, 0);
    Eigen::Vector3f right = up.cross(eyeOffset).normalized();
    up = eyeOffset.cross(right).normalized();
    
    view->lookAt += -0.002 * (x - lastMouseX) * right +
                     0.002 * (y - lastMouseY) * up;
    
    lastMouseX = x;
    lastMouseY = y;
  }
}

void MouseOrbitViewController::MouseUp(WindowCallbacks::MouseButton button, int /*x*/, int /*y*/) {
  if (!view) { return; }
  
  if (button == WindowCallbacks::MouseButton::kLeft) {
    leftMouseButtonPressed = false;
  } else if (button == WindowCallbacks::MouseButton::kMiddle) {
    middleMouseButtonPressed = false;
  } else if (button == WindowCallbacks::MouseButton::kRight) {
    rightMouseButtonPressed = false;
  }
}

void MouseOrbitViewController::WheelRotated(float degrees, WindowCallbacks::Modifier modifiers) {
  if (!view) { return; }
  
  // With emscripten, we got +- 114 degrees here, while with desktop SDL, we got +- 1.
  // So, we just use the sign of the value for now to avoid issues with too large steps.
  degrees = (degrees > 0) ? 1 : -1;
  
  const float factor = (static_cast<int>(modifiers) & static_cast<int>(WindowCallbacks::Modifier::kShift)) ? 0.1f : 1.f;
  
  view->radius = max(0.15f, view->radius * pow(0.85f, factor * degrees));
}

}
