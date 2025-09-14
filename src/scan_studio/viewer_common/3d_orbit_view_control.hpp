#pragma once

#include <vector>

#include <Eigen/Core>

#include <libvis/vulkan/libvis.h>

#include <libvis/util/window_callbacks.h>

#include "scan_studio/viewer_common/touch_gesture_detector.hpp"

namespace scan_studio {
using namespace vis;

/// Defines parameters for camera control for an 'orbit' 3D view:
/// - A "look-at" 3d position where the camera looks at
/// - The camera position, defined using an offset from the look-at position, using the spherical yaw, pitch, and radius parameters
///
/// The default values for this struct are chosen such that with the way these values
/// are used in ComputeEyePosition(), and with a typical field-of-view,
/// a typical human-sized model is fully viewed from the front,
/// assuming that +Y is up and +Z is the model's front, as is the convention for XRVideos.
struct OrbitViewParameters {
  /// Computes the absolute eye position,
  /// by applying the offset defined by (yaw, pitch, radius) to the lookAt position.
  Eigen::Vector3f ComputeEyePosition() const;
  
  /// Computes the eye offset only
  /// (which equals ComputeEyePosition() without adding the lookAt position).
  Eigen::Vector3f ComputeEyeOffset() const;
  
  Eigen::Vector3f lookAt = Eigen::Vector3f(0, 1, 0);
  float yaw = 0;
  float pitch = 0;
  float radius = 3.0f;
};

/// Handles gestures (single finger drag, two finger pinch / move) and translates
/// them into view changes (rotation, zooming, translation).
/// Intended to be used as a GestureCallback for the TouchGestureDetector class.
class TouchOrbitViewController : public GestureCallback {
 public:
  void Initialize(float xdpi, float ydpi, OrbitViewParameters* view);
  
  void SingleFingerDrag(float oldX, float oldY, float newX, float newY) override;
  void TwoFingerPinchOrDrag(float radiusRatio, float centerDiffX, float centerDiffY) override;
  
 private:
  /// Screen dots-per-inch in x and y direction
  float xdpi;
  float ydpi;
  
  OrbitViewParameters* view = nullptr;  // not owned, must remain valid during the class' lifetime
};

/// Handles mouse clicks, motions, and wheel actions, and
/// translates them into view changes (rotation, zooming, translation).
class MouseOrbitViewController {
 public:
  /// Default constructor, requires calling Initialize() before using the class.
  inline MouseOrbitViewController() {}
  
  /// Alternative constructor, allows using the class without calling Initialize().
  inline MouseOrbitViewController(OrbitViewParameters* view)
      : view(view) {}
  
  void Initialize(OrbitViewParameters* view);
  
  void MouseDown(WindowCallbacks::MouseButton button, int x, int y);
  void MouseMove(int x, int y);
  void MouseUp(WindowCallbacks::MouseButton button, int x, int y);
  void WheelRotated(float degrees, WindowCallbacks::Modifier modifiers);
  
 private:
  bool leftMouseButtonPressed = false;
  bool middleMouseButtonPressed = false;
  bool rightMouseButtonPressed = false;
  int lastMouseX;
  int lastMouseY;
  
  OrbitViewParameters* view = nullptr;  // not owned, must remain valid during the class' lifetime
};

}
