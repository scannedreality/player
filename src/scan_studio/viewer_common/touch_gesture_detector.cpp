#include "scan_studio/viewer_common/touch_gesture_detector.hpp"

#include <cmath>

namespace scan_studio {

TouchGestureDetector::TouchGestureDetector(GestureCallback* callback)
    : callback(callback) {}

void TouchGestureDetector::FingerDown(s64 fingerId, float x, float y) {
  fingers.emplace_back(fingerId, x, y);
}

void TouchGestureDetector::FingerMove(s64 fingerId, float x, float y) {
  for (int i = 0; i < fingers.size(); ++ i) {
    FingerState& state = fingers[i];
    
    if (state.id == fingerId) {
      UpdateAction(i, x, y);
      
      state.x = x;
      state.y = y;
      
      return;
    }
  }
}

void TouchGestureDetector::FingerUp(s64 fingerId, float x, float y) {
  for (int i = 0; i < fingers.size(); ++ i) {
    const FingerState& state = fingers[i];
    
    if (state.id == fingerId) {
      UpdateAction(i, x, y);
      
      fingers.erase(fingers.begin() + i);
      return;
    }
  }
}

void TouchGestureDetector::UpdateAction(int fingerIndex, float newX, float newY) {
  const float oldX = fingers[fingerIndex].x;
  const float oldY = fingers[fingerIndex].y;
  
  if (fingers.size() == 1) {
    // Drag action.
    callback->SingleFingerDrag(oldX, oldY, newX, newY);
  } else if (fingers.size() >= 2 && fingerIndex <= 1) {
    // One of the first two fingers of those that are pressed down has moved.
    // Process this as a pinch / two-finger-drag action.
    const float oldCenterX = 0.5f * (fingers[0].x + fingers[1].x);
    const float oldCenterY = 0.5f * (fingers[0].y + fingers[1].y);
    const float oldExtentX = fingers[0].x - fingers[1].x;
    const float oldExtentY = fingers[0].y - fingers[1].y;
    const float oldRadius = sqrtf(oldExtentX * oldExtentX + oldExtentY * oldExtentY);
    
    const float newCenterX = 0.5f * (newX + fingers[1 - fingerIndex].x);
    const float newCenterY = 0.5f * (newY + fingers[1 - fingerIndex].y);
    const float newExtentX = newX - fingers[1 - fingerIndex].x;
    const float newExtentY = newY - fingers[1 - fingerIndex].y;
    const float newRadius = sqrtf(newExtentX * newExtentX + newExtentY * newExtentY);
    
    callback->TwoFingerPinchOrDrag(newRadius / oldRadius, newCenterX - oldCenterX, newCenterY - oldCenterY);
  }
}

}
