include(${SCAN_STUDIO_ROOT_PATH}/cmake/lib_viewer_common_sources.cmake)

set(ScannedRealityViewerWebBaseLib_Sources
  ${VIEWER_COMMON_SOURCES}
  
  third_party/libvis/src/libvis/vulkan/transform_matrices.cc
  third_party/libvis/src/libvis/vulkan/transform_matrices.h
)

set(ENABLE_WASM_PROFILING 0)
set(ENABLE_WASM_SOURCE_MAP 0)
set(ENABLE_WASM_USAN 0)
set(ENABLE_WASM_ASAN 0)
# TODO: Make another option for SAFE_HEAP and ASSERTIONS?   "SHELL:-s ASSERTIONS=1 -s SAFE_HEAP=1"

if (ENABLE_WASM_PROFILING OR ENABLE_WASM_SOURCE_MAP)
  set(EMSCRIPTEN_OPTIMIZATION_LEVEL -O2)
else()
  set(EMSCRIPTEN_OPTIMIZATION_LEVEL -O3)
endif()
set (ScannedRealityViewerWebBaseLib_Options
  $<$<COMPILE_LANGUAGE:CXX>:-DHAVE_OPENGL>
  $<$<COMPILE_LANGUAGE:CXX>:-DHAVE_SDL>
  $<$<COMPILE_LANGUAGE:CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-unqualified-std-cast-call>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:${EMSCRIPTEN_OPTIMIZATION_LEVEL}>
  $<$<COMPILE_LANGUAGE:CXX>:-flto>                             # enable link-time optimization for our program
  $<$<COMPILE_LANGUAGE:CXX>:-pthread>                          # enable threads in emscripten
  $<$<COMPILE_LANGUAGE:CXX>:-DEIGEN_MPL2_ONLY>
  $<$<COMPILE_LANGUAGE:CXX>:SHELL:-sUSE_SDL=0>                 # don't use emscripten's SDL 'port', as we use our own patched version
)
if (ENABLE_WASM_PROFILING)
  set (ScannedRealityViewerWebBaseLib_Options ${ScannedRealityViewerWebBaseLib_Options} $<$<COMPILE_LANGUAGE:CXX>:--profiling>)
endif()
if (ENABLE_WASM_USAN)
  set (ScannedRealityViewerWebBaseLib_Options ${ScannedRealityViewerWebBaseLib_Options} $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=undefined>)
endif()
if (ENABLE_WASM_ASAN)
  set (ScannedRealityViewerWebBaseLib_Options ${ScannedRealityViewerWebBaseLib_Options} $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address>)
endif()
set (ScannedRealityViewerWebBaseLib_IncludeDirectories
  src
  third_party
  third_party/SDL2/include
  third_party/zstd/lib
  third_party/libvis/src
  third_party/libvis/third_party/eigen
  third_party/libvis/third_party/sophus
  third_party/dav1d/include
)
set (ScannedRealityViewerWebBaseLib_Libraries
  libvis_io
  libzstd_static
  SDL2-static
  loguru
)

# Web viewer base lib - Version without SIMD
add_library(scannedreality-viewer-baselib STATIC ${ScannedRealityViewerWebBaseLib_Sources})
target_compile_options(scannedreality-viewer-baselib PUBLIC ${ScannedRealityViewerWebBaseLib_Options})
target_include_directories(scannedreality-viewer-baselib PUBLIC ${ScannedRealityViewerWebBaseLib_IncludeDirectories} ${DAVID_BINARY_DIR}/include/dav1d)  # for dav1d's version.h
target_link_libraries(scannedreality-viewer-baselib PUBLIC ${ScannedRealityViewerWebBaseLib_Libraries} ${DAVID_BINARY_DIR}/src/libdav1d.a openal)
add_dependencies(scannedreality-viewer-baselib dav1d)

# Web viewer base lib - Version with SIMD
add_library(scannedreality-viewer-baselib-simd STATIC ${ScannedRealityViewerWebBaseLib_Sources})
target_compile_options(scannedreality-viewer-baselib-simd PUBLIC ${ScannedRealityViewerWebBaseLib_Options} $<$<COMPILE_LANGUAGE:CXX>:-msimd128>)  # enable WASM SIMD, turns on autovectorization
target_include_directories(scannedreality-viewer-baselib-simd PUBLIC ${ScannedRealityViewerWebBaseLib_IncludeDirectories} ${DAVID_SIMD_BINARY_DIR}/include/dav1d)  # for dav1d's version.h
target_link_libraries(scannedreality-viewer-baselib-simd PUBLIC ${ScannedRealityViewerWebBaseLib_Libraries} ${DAVID_SIMD_BINARY_DIR}/src/libdav1d.a openal)
add_dependencies(scannedreality-viewer-baselib-simd dav1d-simd)


# Web library with a JavaScript API.
# Note that this does not have a main() function, but we still use add_executable() here since this makes
# emscripten create the necessary wasm and js files for being able to load the library from JavaScript.
set(ScannedRealityViewerWeb_Options
  ${EMSCRIPTEN_OPTIMIZATION_LEVEL}  # specify the optimization level here again for JavaScript optimization
  -flto                             # enable link-time optimization for system libraries
  -pthread                          # enable threads in emscripten
  # the `SHELL:` below is to prevent CMake from de-duplicating the -s
  "SHELL:-s PTHREAD_POOL_SIZE=12"   # set pre-defined thread count for emscripten to one thread less than for scannedreality-viewer-app below, since here we don't have the implicit pthread for main()
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"  # allow allocating more memory
  "SHELL:-s MAX_WEBGL_VERSION=2"    # allow using WebGL 2 (emulating OpenGL ES 3.0)
  "SHELL:-s INITIAL_MEMORY=16MB"    # the emscripten default at the time of writing is 16 MB
  "SHELL:-s MAXIMUM_MEMORY=1024MB"  # see below for more comments on INITIAL_MEMORY and MAXIMUM_MEMORY
  "SHELL:-s ENVIRONMENT=web,worker" # do not add code for other environments than the web (that would be unused)
  "SHELL:-s MODULARIZE"             # instead of having the emscripten-generated JavaScript code define a global 'Module' object, have it define a factory function,
                                    # improving the ability to co-exist with other WASM modules
  "SHELL:-s 'EXPORT_NAME=createScannedRealityViewerModule'" # set the name of the module factory function
  "SHELL:-s EXPORTED_FUNCTIONS=_malloc,_free"  # Make sure that module._malloc() etc. will be available
  "SHELL:-s EXPORTED_RUNTIME_METHODS=allocateUTF8,HEAPU8,autoResumeAudioContext,dynCall"  # We need the allocateUTF8() emscripten runtime function to convert a JavaScript string to C++ char*. autoResumeAudioContext,dynCall seem to be required for audio with SDL2.
)
if (ENABLE_WASM_PROFILING)
  set (ScannedRealityViewerWeb_Options
    ${ScannedRealityViewerWeb_Options}
    --profiling -gsource-map --source-map-base http://localhost:8000/lib/  # needs to use http instead of https, otherwise Chrome complains about "invalid authority"
  )
endif()
if (ENABLE_WASM_SOURCE_MAP)
  set (ScannedRealityViewerWeb_Options
    ${ScannedRealityViewerWeb_Options}
    -gsource-map --source-map-base http://localhost:8000/lib/  # needs to use http instead of https, otherwise Chrome complains about "invalid authority"
  )
endif()
if (ENABLE_WASM_USAN)
  set (ScannedRealityViewerWeb_Options ${ScannedRealityViewerWeb_Options} -fsanitize=undefined)
endif()
if (ENABLE_WASM_ASAN)
  set (ScannedRealityViewerWeb_Options ${ScannedRealityViewerWeb_Options} -fsanitize=address)
endif()

# Web library with a JavaScript API - Version without SIMD, and with limited maximum memory to make it work on iOS (see: https://github.com/ffmpegwasm/ffmpeg.wasm/issues/299#issuecomment-1121507264)
add_executable(scannedreality-player src/scan_studio/viewer_web/api_javascript.cpp)
target_link_libraries(scannedreality-player scannedreality-viewer-baselib)
target_link_options(scannedreality-player PRIVATE ${ScannedRealityViewerWeb_Options})
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  add_custom_command(TARGET scannedreality-player POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player.wasm ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player.worker.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/)
endif()

# Web library with a JavaScript API - Version with SIMD
add_executable(scannedreality-player-simd src/scan_studio/viewer_web/api_javascript.cpp)
target_link_libraries(scannedreality-player-simd scannedreality-viewer-baselib-simd)
target_link_options(scannedreality-player-simd PRIVATE ${ScannedRealityViewerWeb_Options})
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  add_custom_command(TARGET scannedreality-player-simd POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-simd.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-simd.wasm ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-simd.worker.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/lib/)
endif()


# Web 'app', written almost fully in C++, includes navigation and playback controls, etc.
set(ScannedRealityViewerAppWeb_Options
  ${EMSCRIPTEN_OPTIMIZATION_LEVEL}
  -flto                             # enable link-time optimization for system libraries
  -pthread                          # enable threads in emscripten
  # the `SHELL:` below is to prevent CMake from de-duplicating the -s
  "SHELL:-s PROXY_TO_PTHREAD"       # run the main() function in a pthread, leaving the actual main thread only for proxied work;
                                    # importantly, this makes thread.join() work in the main() thread without busy-waiting and avoids deadlocks that may occur if other threads try to proxy work
                                    # to it during this time; using PROXY_TO_PTHREAD is recommended here in general: https://emscripten.org/docs/porting/pthreads.html
  "SHELL:-s PTHREAD_POOL_SIZE=13"   # set pre-defined thread count for emscripten to 13 threads:
                                    #     1. pthread running main(), 2. reading thread, 3. video thread, 4. decoding thread, 5. transfer thread,
                                    #     6.-13. dav1d threads (the implementation will use between 4 and 8 of them)
                                    #     note that the main browser thread is separate from those; it will run all WebGL calls and other work proxied to it.
  "SHELL:-s FORCE_FILESYSTEM=1"     # enable emscripten filesystem support
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"  # allow allocating more memory
  "SHELL:-s MAX_WEBGL_VERSION=2"    # allow using WebGL 2 (emulating OpenGL ES 3.0)
  "SHELL:-s INITIAL_MEMORY=16MB"    # the emscripten default at the time of writing is 16 MB
                                    # NOTE: For some reason unknown to me, in Safari on iOS, using INITIAL_MEMORY=512MB causes everything to have EXTREMELY bad performance, even without memory growth!
  "SHELL:-s MAXIMUM_MEMORY=1024MB"  # For shared WASM memory, a maximum must be set.
                                    # For Safari on iOS, this must be below some undocumented limits to work, see:
                                    # https://github.com/ffmpegwasm/ffmpeg.wasm/issues/299#issuecomment-1121507264
  "SHELL:-s ENVIRONMENT=web,worker" # do not add code for other environments than the web (that would be unused)
  "SHELL:-s EXPORTED_RUNTIME_METHODS=autoResumeAudioContext,dynCall"  # autoResumeAudioContext,dynCall seem to be required for audio with SDL2
)
if (ENABLE_WASM_PROFILING)
  set (ScannedRealityViewerAppWeb_Options
    ${ScannedRealityViewerAppWeb_Options}
    --profiling -gsource-map --source-map-base https://localhost/js/  # needs to use http instead of https, otherwise Chrome complains about "invalid authority"
  )
endif()
if (ENABLE_WASM_SOURCE_MAP)
  set (ScannedRealityViewerAppWeb_Options
    ${ScannedRealityViewerAppWeb_Options}
    -gsource-map --source-map-base https://localhost/js/  # needs to use http instead of https, otherwise Chrome complains about "invalid authority"
  )
endif()
if (ENABLE_WASM_USAN)
  set (ScannedRealityViewerAppWeb_Options ${ScannedRealityViewerAppWeb_Options} -fsanitize=undefined)
endif()
if (ENABLE_WASM_ASAN)
  set (ScannedRealityViewerAppWeb_Options ${ScannedRealityViewerAppWeb_Options} -fsanitize=address)
endif()

# Web 'app' - Version without SIMD to make it work with old versions of Safari
add_executable(scannedreality-player-app src/scan_studio/viewer_web/main.cpp)
target_link_libraries(scannedreality-player-app scannedreality-viewer-baselib)
target_link_options(scannedreality-player-app PRIVATE ${ScannedRealityViewerAppWeb_Options})
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  add_custom_command(TARGET scannedreality-player-app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app.wasm ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app.worker.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/)
endif()

# Web 'app' - Version with SIMD
add_executable(scannedreality-player-app-simd src/scan_studio/viewer_web/main.cpp)
target_link_libraries(scannedreality-player-app-simd scannedreality-viewer-baselib-simd)
target_link_options(scannedreality-player-app-simd PRIVATE ${ScannedRealityViewerAppWeb_Options})
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
  add_custom_command(TARGET scannedreality-player-app-simd POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app-simd.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app-simd.wasm ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/scannedreality-player-app-simd.worker.js ${CMAKE_CURRENT_SOURCE_DIR}/dist/scannedreality-player-library-web/app/)
endif()
