#pragma once

#include <vector>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

class GestureCallback {
 public:
  virtual ~GestureCallback() {}
  
  virtual void SingleFingerDrag(float oldX, float oldY, float newX, float newY) = 0;
  virtual void TwoFingerPinchOrDrag(float radiusRatio, float centerDiffX, float centerDiffY) = 0;
};

/// Handles raw touch events (finger down, moved, up) and translates
/// them into gestures (single finger drag, two finger pinch / move).
class TouchGestureDetector {
 public:
  explicit TouchGestureDetector(GestureCallback* callback);
  
  void FingerDown(s64 fingerId, float x, float y);
  void FingerMove(s64 fingerId, float x, float y);
  void FingerUp(s64 fingerId, float x, float y);
  
 private:
  struct FingerState {
    inline FingerState(s64 id, float x, float y)
        : id(id), x(x), y(y) {}
    
    s64 id;
    float x;
    float y;
  };
  
  /// Called when fingers[fingerIndex] moved, with its new coordinates.
  void UpdateAction(int fingerIndex, float newX, float newY);
  
  /// The last known state of all fingers that touch the touch device.
  /// Stored in the order of when they started touching the device.
  /// If this contains at least two entries, then the first two entries
  /// are the fingers used for a pinch gesture.
  vector<FingerState> fingers;
  
  GestureCallback* callback;  // not owned, must remain valid during the class' lifetime
};

}
