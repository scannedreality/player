#pragma once

#include <memory>
#include <vector>

#include <libvis/util/window_callbacks.h>
#include <libvis/vulkan/libvis.h>

struct SDL_Window;

namespace scan_studio {
using namespace vis;

/// Base class for SDL-based render windows.
/// Note that there is also a similar RenderWindow class in libvis, but we do not use that
/// here since we do not want to include libvis for the viewer targets.
class RenderWindowSDL {
 public:
  enum class WindowState {
    Default,
    Maximized,
    Fullscreen
  };
  
  virtual inline ~RenderWindowSDL() {};
  
  /// Initializes the window and the rendering API.
  virtual bool Initialize(
      const char* title,
      int width,
      int height,
      WindowState windowState,
      const shared_ptr<WindowCallbacks>& callbacks);
  
  virtual void Deinitialize() = 0;
  
  /// Changes the window callbacks object.
  void SetCallbacks(const shared_ptr<WindowCallbacks>& callbacks);
  
  /// Runs a render loop until the window is closed.
  void Exec();
  
  /// Does one main loop iteration. Returns false if the main loop should be exited.
  bool MainLoopIteration();
  
  /// Tells the render window's event loop about the SDL event number that shall be interpreted as custom callback events.
  /// This allows enqueuing callbacks that will be executed on the main thread (even while the window is minimized).
  ///
  /// The event number must have been registered with SDL_RegisterEvents() before.
  /// SetCustomCallbackEvent() must be called before calling Exec().
  ///
  /// Upon receiving a custom callback event, the members of SDL_UserEvent are interpreted as follows:
  /// void* data1: Pointer to a std::function<void()>, allocated with new.
  ///              The event loop will execute this function on the main thread, and then delete the std::function.
  void SetCustomCallbackEvent(u32 eventType);
  
  /// Sets the window icon to a 32-bit-per-pixel RGBA image in memory
  bool SetIconFromMemory(void* pixelData, u32 width, u32 height);
  
#ifndef __EMSCRIPTEN__
  /// Sets the window icon to an AVIF image
  /// (this image format was chosen because we already ship a decoder for it,
  /// as used for XRVideo decoding: dav1d).
  bool SetIconFromAVIFResource(const void* data, usize size);
#endif
  
  inline void Close() { closed = true; }
  
  inline SDL_Window* GetWindow() { return window_; }
  
 protected:
  virtual bool InitializeImpl(
      const char* title,
      int width,
      int height,
      WindowState windowState) = 0;
  
  /// Must return the size of the drawable area in pixels. For high-DPI displays,
  /// that may be more than the size reported by SDL_GetWindowSize().
  virtual void GetDrawableSize(int* width, int* height) = 0;
  
  virtual inline void Resize(int width, int height) { (void) width; (void) height; }
  
  virtual void Render() = 0;
  
  int window_area_width = 0;
  int window_area_height = 0;
  
  bool closed;
  
  u32 customCallbackEventType = (u32)-1;
  
  shared_ptr<WindowCallbacks> callbacks_;
  
  SDL_Window* window_ = nullptr;
};

}
