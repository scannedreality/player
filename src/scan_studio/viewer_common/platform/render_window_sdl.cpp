#include "scan_studio/viewer_common/platform/render_window_sdl.hpp"

#include <cstring>
#include <functional>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <loguru.hpp>

#include <SDL.h>

#ifndef __EMSCRIPTEN__
#include <avif/avif.h>
#endif

namespace scan_studio {

#ifdef TARGET_OS_IOS
static int HandleAppEvents(void* userdata, SDL_Event* event) {
  RenderWindowSDL* renderWindow = reinterpret_cast<RenderWindowSDL*>(userdata);
  
  // LOG(1) << "SDL event, type: " << event->type;
  
  switch (event->type)   {
  case SDL_APP_TERMINATING:
    // Terminate the app.
    // Shut everything down before returning from this function.
    LOG(1) << "SDL_APP_TERMINATING ...";
    renderWindow->Deinitialize();
    return 0;
  case SDL_APP_LOWMEMORY:
    // You will get this when your app is paused and iOS wants more memory.
    // Release as much memory as possible.
    LOG(1) << "SDL_APP_LOWMEMORY ...";
    // TODO: handle this
    return 0;
  case SDL_APP_WILLENTERBACKGROUND:
    // Prepare your app to go into the background.  Stop loops, etc.
    // This gets called when the user hits the home button, or gets a call.
    LOG(1) << "SDL_APP_WILLENTERBACKGROUND ...";
    // TODO: handle this
    return 0;
  case SDL_APP_DIDENTERBACKGROUND:
    // This will get called if the user accepted whatever sent your app to the background.
    // If the user got a phone call and canceled it, you'll instead get an SDL_APP_DIDENTERFOREGROUND event and restart your loops.
    // When you get this, you have 5 seconds to save all your state or the app will be terminated.
    // Your app is NOT active at this point.
    LOG(1) << "SDL_APP_DIDENTERBACKGROUND ...";
    // TODO: handle this
    return 0;
  case SDL_APP_WILLENTERFOREGROUND:
    // This call happens when your app is coming back to the foreground.
    // Restore all your state here.
    LOG(1) << "SDL_APP_WILLENTERFOREGROUND ...";
    // TODO: handle this
    return 0;
  case SDL_APP_DIDENTERFOREGROUND:
    // Restart your loops here.
    // Your app is interactive and getting CPU again.
    LOG(1) << "SDL_APP_DIDENTERFOREGROUND ...";
    // TODO: handle this
    return 0;
  default:
    /* No special processing, add it to the event queue */
    return 1;
  }
}
#endif

bool RenderWindowSDL::Initialize(const char* title, int width, int height, WindowState windowState, const shared_ptr<WindowCallbacks>& callbacks) {
  callbacks_ = callbacks;
  callbacks->SetWindow(this);
  
  // Tell SDL that we can handle touch events
  SDL_EventState(SDL_FINGERDOWN, SDL_ENABLE);
  SDL_EventState(SDL_FINGERMOTION, SDL_ENABLE);
  SDL_EventState(SDL_FINGERUP, SDL_ENABLE);
  
  // Disable the annoying default behavior on the iPad where one has to swipe twice from the bottom-left to exit the app, with unclear timing
  #ifdef TARGET_OS_IOS
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "0");
  #endif
  
  if (!InitializeImpl(title, width, height, windowState)) {
    return false;
  }
  
  return true;
}

void MainLoopIterationCaller(void* data) {
  RenderWindowSDL* renderWindow = reinterpret_cast<RenderWindowSDL*>(data);
  renderWindow->MainLoopIteration();
}

void RenderWindowSDL::SetCallbacks(const shared_ptr<vis::WindowCallbacks>& callbacks) {
  callbacks_ = callbacks;
}

void RenderWindowSDL::Exec() {
  // As documented in SDL2/docs/README-ios.md, to play well with the iOS app lifecycle,
  // it is required to register an event filter to be able to directly respond to iOS app callbacks.
  #ifdef TARGET_OS_IOS
    SDL_SetEventFilter(HandleAppEvents, /*userdata*/ this);
  #endif
  
  closed = false;
  
  #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(MainLoopIterationCaller, this, /*fps - should be zero*/ 0, /*simulate_infinite_loop*/ true);
  #else
    while (true) {
      if (!MainLoopIteration()) {
        return;
      }
    }
  #endif
}

bool RenderWindowSDL::MainLoopIteration() {
  // HACK: In emscripten, when running on mobile devices, we get mouse-down events for touches
  //       that act like clicks (not moving the finger, and releasing soon). They seem to have coordinates in CSS pixels,
  //       meaning that they will be interpreted wrongly on high-DPI displays where these do not correspond to render pixels.
  //       They have event.button.which == 0, which is different from SDL_TOUCH_MOUSEID, so that cannot be used to filter them out.
  //       As a workaround, we completely ignore all mouse events here if our 'ANY_HOVER_INPUT' flag is set by the JavaScript side.
  //       This means that attaching a mouse to a mobile device to control the web app would not work.
  #ifdef __EMSCRIPTEN__
    const bool disableMouseEvents = getenv("ANY_HOVER_INPUT")[0] == '0';
  #else
    constexpr bool disableMouseEvents = false;
  #endif
  
  auto getModifiers = [&]() {
    int modifiers = 0;
    if ((SDL_GetModState() & KMOD_SHIFT) != 0) { modifiers |= static_cast<int>(WindowCallbacks::Modifier::kShift); }
    if ((SDL_GetModState() & KMOD_CTRL) != 0) { modifiers |= static_cast<int>(WindowCallbacks::Modifier::kCtrl); }
    if ((SDL_GetModState() & KMOD_ALT) != 0) { modifiers |= static_cast<int>(WindowCallbacks::Modifier::kAlt); }
// #ifndef _WIN32
//   if ((SDL_GetModState() & KMOD_GUI) != 0) { modifiers |= static_cast<int>(RenderWindowCallbacks::Modifier::kSuper); }
// #endif
    return static_cast<WindowCallbacks::Modifier>(modifiers);
  };
  
  // Poll and handle events (inputs, window resize, etc.)
  bool resizeHappened = false;
  SDL_Event event;
  
  while (SDL_PollEvent(&event)) {
    callbacks_->SDLEvent(&event);
    
    // Process input and window state events
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
      if (disableMouseEvents || event.button.which == SDL_TOUCH_MOUSEID) { continue; }  // ignore simulated mouse events generated by touch events
      
      WindowCallbacks::MouseButton button = WindowCallbacks::MouseButton::kInvalid;
      switch (event.button.button) {
      case SDL_BUTTON_LEFT: button = WindowCallbacks::MouseButton::kLeft; break;
      case SDL_BUTTON_MIDDLE: button = WindowCallbacks::MouseButton::kMiddle; break;
      case SDL_BUTTON_RIGHT: button = WindowCallbacks::MouseButton::kRight; break;
      }
      
      if (button != WindowCallbacks::MouseButton::kInvalid) {
        if (event.type == SDL_MOUSEBUTTONDOWN) {
          callbacks_->MouseDown(button, event.button.x, event.button.y, event.button.clicks);
        } else {  // if (event.type == SDL_MOUSEBUTTONUP) {
          callbacks_->MouseUp(button, event.button.x, event.button.y, event.button.clicks);
        }
      }
    } else if (event.type == SDL_MOUSEMOTION) {
      if (disableMouseEvents || event.button.which == SDL_TOUCH_MOUSEID) { continue; }  // ignore simulated mouse events generated by touch events
      
      callbacks_->MouseMove(event.motion.x, event.motion.y);
    } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
      // TODO: The scroll amounts should have the same magnitude as those reported by the Qt-based implementation!
      callbacks_->WheelRotated(event.wheel.y, getModifiers());
    } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      const auto key = SDL_GetKeyFromScancode(event.key.keysym.scancode);
      if (event.type == SDL_KEYDOWN) {
        callbacks_->KeyPressed(key, getModifiers());
      } else {  // if (event.type == SDL_KEYUP) {
        callbacks_->KeyReleased(key, getModifiers());
      }
    } else if (event.type == SDL_TEXTINPUT) {
      callbacks_->TextInput(event.text.text);
    } else if (event.type == SDL_FINGERDOWN) {
      if (event.tfinger.touchId == SDL_MOUSE_TOUCHID) { continue; }  // ignore simulated touch events generated by mouse events
      
      callbacks_->FingerDown(event.tfinger.fingerId, event.tfinger.x * window_area_width, event.tfinger.y * window_area_height);
    } else if (event.type == SDL_FINGERMOTION) {
      if (event.tfinger.touchId == SDL_MOUSE_TOUCHID) { continue; }  // ignore simulated touch events generated by mouse events
      
      callbacks_->FingerMove(event.tfinger.fingerId, event.tfinger.x * window_area_width, event.tfinger.y * window_area_height);
    } else if (event.type == SDL_FINGERUP) {
      if (event.tfinger.touchId == SDL_MOUSE_TOUCHID) { continue; }  // ignore simulated touch events generated by mouse events
      
      callbacks_->FingerUp(event.tfinger.fingerId, event.tfinger.x * window_area_width, event.tfinger.y * window_area_height);
    } else if (event.type == customCallbackEventType) {
      std::function<void()>* callback = reinterpret_cast<std::function<void()>*>(event.user.data1);
      (*callback)();
      delete callback;
    } else if (event.type == SDL_QUIT) {
      Deinitialize();
      return false;
    } else if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window_)) {
      // Window events have a separate sub-type that is stored in event.window.event.
      if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
        // Pause rendering until the window is restored.
        // Custom callback events are still handled.
        while (SDL_WaitEvent(&event)) {
          if (event.type == SDL_QUIT) {
            Deinitialize();
            return false;
          } else if (event.type == customCallbackEventType) {
            std::function<void()>* callback = reinterpret_cast<std::function<void()>*>(event.user.data1);
            (*callback)();
            delete callback;
          } else if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window_) && event.window.event == SDL_WINDOWEVENT_RESTORED) {
            break;
          }
        }
      } else if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
        GetDrawableSize(&window_area_width, &window_area_height);
        resizeHappened = true;
      }
    }
  }  // while (SDL_PollEvent(&event))
  
  if (closed) {
    Deinitialize();
    return false;
  }
  
  // Process the last resize event only
  if (resizeHappened) {
    Resize(window_area_width, window_area_height);
    callbacks_->Resize(window_area_width, window_area_height);
  }
  
  Render();
  
  return true;
}

void RenderWindowSDL::SetCustomCallbackEvent(u32 eventType) {
  customCallbackEventType = eventType;
}

bool RenderWindowSDL::SetIconFromMemory(void* pixelData, u32 width, u32 height) {
  constexpr u32 rMask = 0x000000ff;
  constexpr u32 gMask = 0x0000ff00;
  constexpr u32 bMask = 0x00ff0000;
  constexpr u32 aMask = 0xff000000;
  SDL_Surface* iconSurface = SDL_CreateRGBSurfaceFrom(pixelData, width, height, /*depth [bits/pixel]*/ 32, /*pitch [bytes/row]*/ 4 * width, rMask, gMask, bMask, aMask);
  if (iconSurface == nullptr) {
    LOG(ERROR) << "SDL_CreateRGBSurfaceFrom() failed";
    return false;
  }
  SDL_SetWindowIcon(window_, iconSurface);
  SDL_FreeSurface(iconSurface);
  return true;
}

#ifndef __EMSCRIPTEN__
bool RenderWindowSDL::SetIconFromAVIFResource(const void* data, usize size) {
  avifDecoder* decoder = avifDecoderCreate();
  decoder->maxThreads = 1;
  decoder->ignoreExif = true;
  decoder->ignoreXMP = true;
  
  avifResult result = avifDecoderSetIOMemory(decoder, static_cast<const u8*>(data), size);
  if (result != AVIF_RESULT_OK) {
    LOG(ERROR) << "avifDecoderSetIOMemory() failed"; avifDecoderDestroy(decoder); return false;
  }
  
  result = avifDecoderParse(decoder);
  if (result != AVIF_RESULT_OK) {
    LOG(ERROR) << "Failed to decode image: " << avifResultToString(result); avifDecoderDestroy(decoder); return false;
  }
  
  // Now available:
  // * All decoder->image information other than pixel data:
  //   * width, height, depth
  //   * transformations (pasp, clap, irot, imir)
  //   * color profile (icc, CICP)
  //   * metadata (Exif, XMP)
  // * decoder->alphaPresent
  // * number of total images in the AVIF (decoder->imageCount)
  // * overall image sequence timing (including per-frame timing with avifDecoderNthImageTiming())
  
  if (avifDecoderNextImage(decoder) != AVIF_RESULT_OK) {
    LOG(ERROR) << "No image present"; avifDecoderDestroy(decoder); return false;
  }
  
  // Now available (for this frame):
  // * All decoder->image YUV pixel data (yuvFormat, yuvPlanes, yuvRange, yuvChromaSamplePosition, yuvRowBytes)
  // * decoder->image alpha data (alphaRange, alphaPlane, alphaRowBytes)
  // * this frame's sequence timing
  
  avifRGBImage rgb;
  memset(&rgb, 0, sizeof(rgb));
  avifRGBImageSetDefaults(&rgb, decoder->image);
  // Override YUV(A)->RGB(A) defaults here: depth, format, chromaUpsampling, ignoreAlpha, alphaPremultiplied, libYUVUsage, etc
  rgb.ignoreAlpha = false;
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  
  // Alternative: set rgb.pixels and rgb.rowBytes yourself, which should match your chosen rgb.format
  // Be sure to use uint16_t* instead of uint8_t* for rgb.pixels/rgb.rowBytes if (rgb.depth > 8)
  avifRGBImageAllocatePixels(&rgb);
  
  if (avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK) {
    LOG(ERROR) << "Conversion from YUV failed"; avifRGBImageFreePixels(&rgb); avifDecoderDestroy(decoder); return false;
  }
  
  // Now available:
  // * RGB(A) pixel data (rgb.pixels, rgb.rowBytes)
  
  const bool setIconResult = SetIconFromMemory(rgb.pixels, decoder->image->width, decoder->image->height);
  
  avifRGBImageFreePixels(&rgb);
  avifDecoderDestroy(decoder);
  return setIconResult;
}
#endif

}
