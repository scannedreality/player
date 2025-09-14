// Copyright 2017, 2019 ETH Zürich, Thomas Schöps
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#pragma once

#include <string>
#include <vector>

#include "libvis/libvis.h"

// QT_BEGIN_NAMESPACE
class QPainter;
// QT_END_NAMESPACE

typedef union SDL_Event SDL_Event;

typedef uint32_t VkFlags;
struct VkPhysicalDeviceFeatures;
typedef VkFlags VkSampleCountFlags;

namespace vis {

class ImageDisplayQtWindow;
class RenderWindow;
class VulkanPhysicalDevice;

/// Interface which can be subclassed to receive render window callbacks.
/// Attention: these callbacks are called from the Qt thread (if Qt is used as
/// window toolkit), and therefore likely from a different thread than the main
/// thread.
class WindowCallbacks {
 public:
  enum class MouseButton {
    kLeft    = 1 << 0,
    kMiddle  = 1 << 1,
    kRight   = 1 << 2,
    kInvalid = 1 << 3,
  };
  
  enum class Modifier {
    kShift = 1 << 0,
    kCtrl  = 1 << 1,
    kAlt   = 1 << 2
  };
  
  virtual ~WindowCallbacks() = default;
  
  // --- Configuration callbacks for Vulkan ---
  // TODO: Can we avoid having too many rendering API specific callbacks here?
  //       Perhaps by making a separate callback base class for Vulkan-specific callbacks?
  
  /// Used by the Vulkan render window only.
  /// This function may optionally extend the given lists of instance and device
  /// extensions to request the desired *required* extensions. This will make the physical device selection
  /// process consider only those devices that support these extensions. For *optional* extensions, use PreInitialize()
  /// to add them if they are available for the selected physical device.
  virtual void SpecifyAdditionalExtensions(vector<string>* instanceExtensions, vector<string>* deviceExtensions) {
    (void) instanceExtensions;
    (void) deviceExtensions;
  }
  
  /// Used by the Vulkan render window only.
  /// This function is called before the render window is initialized. It may be used to configure the
  /// enabled features for the Vulkan instance and logical Vulkan device, add any optional extensions
  /// that the given physical device supports, and add structures to the pNext pointer of VkDeviceCreateInfo.
  virtual void PreInitialize(VulkanPhysicalDevice* physical_device, VkSampleCountFlags* msaaSamples, VkPhysicalDeviceFeatures* features_to_enable, vector<string>* deviceExtensions, const void** deviceCreateInfoPNext) {
    (void) physical_device;
    (void) msaaSamples;
    (void) features_to_enable;
    (void) deviceExtensions;
    (void) deviceCreateInfoPNext;
  }
  
  // --- Init / Destroy callbacks ---
  
  /// Tells the callback object about the window object it is used with.
  /// The type of the window object depends on the class it is used with.
  virtual void SetWindow(void* window) {
    (void) window;
  }
  
  /// Called when the render window is initialized. Can be used to allocate
  /// rendering API resources that require an OpenGL context respectively a Vulkan device.
  /// For Vulkan, this is only called once at window creation, not when the swap chain is re-created.
  /// If this returns false, initialization will be aborted.
  virtual bool Initialize() { return true; }
  
  /// Used by the Vulkan render window only.
  /// Objects that depend on the surface and dependent objects and their properties,
  /// such as for example the number of swap chain images,
  /// must be initialized in this function.
  /// If this returns false during the initial call to InitializeSurfaceDependent(), initialization will be aborted.
  virtual bool InitializeSurfaceDependent() { return true; }
  
  /// Called when the render window is destroyed. Can be used to deallocate
  /// rendering API resources that require an OpenGL context respectively a Vulkan device.
  /// For Vulkan, this is only called once at window destruction, not when the swap chain is re-created.
  virtual void Deinitialize() {}
  
  /// Used by the Vulkan render window only.
  /// Objects that depend on the surface and dependent objects and their properties,
  /// such as for example the number of swap chain images,
  /// must be deinitialized in this function.
  virtual void DeinitializeSurfaceDependent() {}
  
  // --- Resize and Render ---
  
  /// Called after the window is resized. Can be used to re-allocate rendering
  /// API resources with the right size.
  virtual void Resize(int width, int height) {
    (void) width;
    (void) height;
  }
  
  /// Called when the scene shall be rendered (with a graphics API such as OpenGL or Vulkan).
  /// In case of Vulkan, the given image index refers to the index of the swap chain image that will be rendered to.
  /// In case of OpenGL, the parameter is unused and should be ignored.
  virtual void Render(int image_index) {
    (void) image_index;
  }
  
  // --- API-specific render / event callbacks ---
  
  /// For QT only: This callback may paint on top of the displayed image with the QPainter API.
  /// NOTE: Do not use `#ifdef LIBVIS_HAVE_QT` here to make this function conditional.
  ///       This may cause inconsistent views on this class' virtual function table if some
  ///       other compilation units have inconsistent states of LIBVIS_HAVE_QT.
  virtual void Render(QPainter* /*painter*/) {}
  
  /// For SDL only: Called for each SDL event.
  virtual void SDLEvent(SDL_Event* /*event*/) {}
  
  // --- Mouse click and move callbacks ---
  
  /// Called when the user presses a mouse button.
  /// clickCount is 1 for the first click, 2 for a double-click, and so on.
  virtual void MouseDown(MouseButton button, int x, int y, int clickCount) {
    (void) button;
    (void) x;
    (void) y;
    (void) clickCount;
  }
  
  /// Called when the user moves the mouse over the window (both if a mouse
  /// button is pressed and if not).
  virtual void MouseMove(int x, int y) {
    (void) x;
    (void) y;
  }
  
  /// Called when the user releases a mouse button.
  /// clickCount is 1 for the first click, 2 for a double-click, and so on.
  virtual void MouseUp(MouseButton button, int x, int y, int clickCount) {
    (void) button;
    (void) x;
    (void) y;
    (void) clickCount;
  }
  
  // --- Finger touch callbacks ---
  
  /// Called when the user starts touching a touch device with a finger.
  virtual void FingerDown(s64 finger_id, float x, float y) {
    (void) finger_id;
    (void) x;
    (void) y;
  }
  
  /// Called when the user moves a finger on a touch device.
  virtual void FingerMove(s64 finger_id, float x, float y) {
    (void) finger_id;
    (void) x;
    (void) y;
  }
  
  /// Called when the user stops touching a touch device with a finger.
  virtual void FingerUp(s64 finger_id, float x, float y) {
    (void) finger_id;
    (void) x;
    (void) y;
  }
  
  // --- Mouse wheel callbacks ---
  
  /// Called when the mouse wheel is rotated.
  virtual void WheelRotated(float degrees, Modifier modifiers) {
    (void) degrees;
    (void) modifiers;
  }
  
  // --- Key callbacks ---
  
  /// Called when a key is pressed.
  /// The int keycode should have ASCII values for those keys which map to such a character.
  /// For other characters, its values may be specific to the implementation; for example,
  /// for SDL, the values are SDL_Keycode values.
  virtual void KeyPressed(int key, Modifier modifiers) {
    (void) key;
    (void) modifiers;
  }
  
  /// Called when a key is released.
  /// See KeyPressed() for details on the keycode.
  virtual void KeyReleased(int key, Modifier modifiers) {
    (void) key;
    (void) modifiers;
  }
  
  /// Called when text has been input.
  /// This exists (in addition to KeyPressed()) for example in order to support characters
  /// that only get emitted after pressing two keys.
  virtual void TextInput(const char* text) {
    (void) text;
  }
};

}
