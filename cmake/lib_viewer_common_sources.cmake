set(VIEWER_COMMON_SRC_PATH "${SCAN_STUDIO_ROOT_PATH}/src/scan_studio/viewer_common")

set(VIEWER_COMMON_SOURCES
  # TODO: Put these into a library that both `common` and `viewer_common` depend on:
  ${VIEWER_COMMON_SRC_PATH}/../common/xrvideo_file.cpp
  ${VIEWER_COMMON_SRC_PATH}/../common/xrvideo_file.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/../common/wav_sound.cpp  # TODO
  ${VIEWER_COMMON_SRC_PATH}/audio/audio_sdl.cpp
  ${VIEWER_COMMON_SRC_PATH}/audio/audio_sdl.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_library.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_library.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_opengl.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_opengl.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_shader.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_shader.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_vulkan.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_vulkan.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_opengl.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_opengl.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_shader.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_shader.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_vulkan.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_vulkan.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_opengl.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_opengl.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_vulkan.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_vulkan.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_shader.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_shader.hpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_vulkan.cpp
  ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_vulkan.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/license_texts/texts.cpp
  ${VIEWER_COMMON_SRC_PATH}/license_texts/texts.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/openxr/action_set.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/action_set.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/instance.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/instance.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/loader.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/openxr.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/openxr.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/openxr_vulkan_application.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/openxr_vulkan_application.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/session.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/session.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/space.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/space.hpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/swapchain.cpp
  ${VIEWER_COMMON_SRC_PATH}/openxr/swapchain.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/decoded_frame_cache.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/decoding_thread.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/frame_loading.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/frame_loading.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/frame_loading_webcodecs.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/frame_loading_webcodecs.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/index.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/index.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/playback_state.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/playback_state.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/reading_thread.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/transfer_thread.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/video_thread.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/video_thread.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/xrvideo.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/xrvideo.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/xrvideo_common_resources.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo_common_resources.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo_common_resources.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo_frame.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/vulkan/vulkan_xrvideo_frame.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/external/external_xrvideo.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/external/external_xrvideo.hpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/external/external_xrvideo_frame.cpp
  ${VIEWER_COMMON_SRC_PATH}/xrvideo/external/external_xrvideo_frame.hpp
  
  ${VIEWER_COMMON_SRC_PATH}/3d_orbit_view_control.cpp
  ${VIEWER_COMMON_SRC_PATH}/3d_orbit_view_control.hpp
  ${VIEWER_COMMON_SRC_PATH}/common.cpp
  ${VIEWER_COMMON_SRC_PATH}/common.hpp
  ${VIEWER_COMMON_SRC_PATH}/debug.hpp
  ${VIEWER_COMMON_SRC_PATH}/display_flatscreen.cpp
  ${VIEWER_COMMON_SRC_PATH}/display_flatscreen.hpp
  ${VIEWER_COMMON_SRC_PATH}/display_xr.cpp
  ${VIEWER_COMMON_SRC_PATH}/display_xr.hpp
  ${VIEWER_COMMON_SRC_PATH}/http_request.hpp
  ${VIEWER_COMMON_SRC_PATH}/license_texts.cpp
  ${VIEWER_COMMON_SRC_PATH}/license_texts.hpp
  ${VIEWER_COMMON_SRC_PATH}/render_state.cpp
  ${VIEWER_COMMON_SRC_PATH}/render_state.hpp
  ${VIEWER_COMMON_SRC_PATH}/streaming_input_stream.cpp
  ${VIEWER_COMMON_SRC_PATH}/streaming_input_stream.hpp
  ${VIEWER_COMMON_SRC_PATH}/timing.hpp
  ${VIEWER_COMMON_SRC_PATH}/touch_gesture_detector.cpp
  ${VIEWER_COMMON_SRC_PATH}/touch_gesture_detector.hpp
  ${VIEWER_COMMON_SRC_PATH}/ui_common.cpp
  ${VIEWER_COMMON_SRC_PATH}/ui_common.hpp
  ${VIEWER_COMMON_SRC_PATH}/ui_flatscreen.cpp
  ${VIEWER_COMMON_SRC_PATH}/ui_flatscreen.hpp
  ${VIEWER_COMMON_SRC_PATH}/ui_icons.cpp
  ${VIEWER_COMMON_SRC_PATH}/ui_icons.hpp
  ${VIEWER_COMMON_SRC_PATH}/ui_xr.cpp
  ${VIEWER_COMMON_SRC_PATH}/ui_xr.hpp
  ${VIEWER_COMMON_SRC_PATH}/util.cpp
  ${VIEWER_COMMON_SRC_PATH}/util.hpp
)

if (NOT ANDROID)
  set(VIEWER_COMMON_SOURCES ${VIEWER_COMMON_SOURCES}
    ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl.cpp
    ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl.hpp
  )
endif()

if (WIN32)
  set(VIEWER_COMMON_SOURCES ${VIEWER_COMMON_SOURCES}
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_common_resources.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_common_resources.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_frame.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_frame.hpp
    # ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_shader.cpp
    # ${VIEWER_COMMON_SRC_PATH}/xrvideo/d3d11/d3d11_xrvideo_shader.hpp
  )
endif()

if ((TARGET OpenGL::GL) OR ANDROID OR EMSCRIPTEN)
  set(VIEWER_COMMON_SOURCES ${VIEWER_COMMON_SOURCES}
    ${VIEWER_COMMON_SRC_PATH}/opengl/buffer.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/buffer.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_cgl.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_cgl.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_egl.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_egl.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_glx.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_glx.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_sdl.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_sdl.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_wgl.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/context_wgl.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/extensions.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/extensions.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/loader.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/loader.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/shader.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/shader.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/texture.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/texture.hpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/util.cpp
    ${VIEWER_COMMON_SRC_PATH}/opengl/util.hpp
    
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_common_resources.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_common_resources.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_frame.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_frame.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_shader.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/opengl/opengl_xrvideo_shader.hpp
  )
  
  if (NOT ANDROID)
    set(VIEWER_COMMON_SOURCES ${VIEWER_COMMON_SOURCES}
      ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl_opengl.cpp
      ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl_opengl.hpp
    )
  endif()
endif()

if (APPLE)
  set(VIEWER_COMMON_SOURCES ${VIEWER_COMMON_SOURCES}
    ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_metal.cpp
    ${VIEWER_COMMON_SRC_PATH}/gfx/fontstash_metal.hpp
    ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_metal.cpp
    ${VIEWER_COMMON_SRC_PATH}/gfx/shape2d_metal.hpp
    ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_metal.cpp
    ${VIEWER_COMMON_SRC_PATH}/gfx/text2d_metal.hpp
    # ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_metal.cpp
    # ${VIEWER_COMMON_SRC_PATH}/gfx/textured_shape2d_metal.hpp
    
    ${VIEWER_COMMON_SRC_PATH}/metal/library_cache.cpp
    ${VIEWER_COMMON_SRC_PATH}/metal/library_cache.hpp
    
    ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl_metal.cpp
    ${VIEWER_COMMON_SRC_PATH}/platform/render_window_sdl_metal.hpp
    
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo_common_resources.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo_common_resources.hpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo_frame.cpp
    ${VIEWER_COMMON_SRC_PATH}/xrvideo/metal/metal_xrvideo_frame.hpp
  )
endif()
