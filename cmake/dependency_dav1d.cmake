# This library uses meson as its build tool, thus we have to include it as an external project.

# ----------------------------------------------------------------------------------------- #
# Attention: At least on macOS, meson fails to re-configure when settings are changed here. #
#            Thus, clean the build dir to make sure that changes actually have an effect!   #
# ----------------------------------------------------------------------------------------- #

if (APPLE)
  # Using just "meson" did not work, even with it being in PATH, as it assumed itself to be in a wrong directory during dav1d's ninja build then
  set(MESON_PATH "/usr/local/bin/meson")
  
  set(DAVID_ADDITIONAL_OPTIONS "")
  
  if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Enable cross-compilation for iOS.
    # Find the iOS SDK location using xcrun
    find_program(XCRUN xcrun)
    execute_process(
        COMMAND ${XCRUN} --sdk iphoneos --show-sdk-path
        OUTPUT_VARIABLE IPHONEOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    
    # Configure the meson cross file
    set(MESON_SYSTEM "darwin")
    set(MESON_CPU_FAMILY "aarch64")
    set(MESON_CPU "aarch64")
    
    set(MESON_C_FLAGS "-target arm64-apple-ios -arch arm64 --sysroot=${IPHONEOS_SDK_PATH} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
    separate_arguments(MESON_C_FLAGS)
    list(JOIN MESON_C_FLAGS "', '" MESON_C_FLAGS)
    
    configure_file(${SCAN_STUDIO_ROOT_PATH}/other/dav1d-cross.meson.in ${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson)
    
    set(DAVID_ADDITIONAL_OPTIONS "--cross-file=${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson")
    
    set(DAV1D_ARCH arm64)
  else()
    if (CMAKE_OSX_ARCHITECTURES STREQUAL x86_64)
      # Configure the meson cross file - this is assuming that we build on an Apple Silicon Mac, not on an Intel Mac
      set(MESON_SYSTEM "darwin")
      set(MESON_CPU_FAMILY "x86_64")
      set(MESON_CPU "x86_64")
      
      set(MESON_C_FLAGS "-target x86_64-apple-macos -arch x86_64 -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
      separate_arguments(MESON_C_FLAGS)
      list(JOIN MESON_C_FLAGS "', '" MESON_C_FLAGS)
      
      configure_file(${SCAN_STUDIO_ROOT_PATH}/other/dav1d-cross.meson.in ${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson)
      
      set(DAVID_ADDITIONAL_OPTIONS "--cross-file=${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson")
    endif()
    
    set(DAV1D_ARCH ${CMAKE_OSX_ARCHITECTURES})
  endif()
  
  # Using CFLAGS="-arch arm64" works around that issue:
  # https://code.videolan.org/videolan/dav1d/-/issues/373
  #
  # We however need to use an external script to be able to use "-arch arm64" in the ExternalProject_Add(),
  # since CMake insists in escaping that in ways that will break it.
  # Thus, I had to put the CONFIGURE_COMMAND into the external script dependency_dav1d_configure.sh.
  
  ExternalProject_Add(dav1d
    SOURCE_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d
    CONFIGURE_COMMAND  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_dav1d_configure.sh ${CMAKE_COMMAND} ${DAV1D_ARCH} ${CMAKE_OSX_DEPLOYMENT_TARGET} ${MESON_PATH} ${CMAKE_CURRENT_SOURCE_DIR} ${DAVID_ADDITIONAL_OPTIONS}
    BUILD_COMMAND      ninja
    BUILD_ALWAYS       TRUE
    INSTALL_COMMAND    ""
    TEST_COMMAND       ""
    BUILD_BYPRODUCTS   ${CMAKE_CURRENT_BINARY_DIR}/dav1d-prefix/src/dav1d-build/src/libdav1d.a
  )
  ExternalProject_Get_property(dav1d BINARY_DIR)
  set(DAVID_BINARY_DIR ${BINARY_DIR})
elseif ("${CMAKE_ANDROID_ARCH_ABI}" STREQUAL "")
  # Build for desktop or web
  
  set(DAVID_ADDITIONAL_OPTIONS "")
  if (EMSCRIPTEN)
    set(DAVID_ADDITIONAL_OPTIONS --cross-file=../../../../../other/dav1d-wasm-cross.txt)
  endif()
  
  ExternalProject_Add(dav1d
    SOURCE_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d
    CONFIGURE_COMMAND  meson setup ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d --buildtype release --default-library=static -Denable_tests=false -Denable_tools=false -Dbitdepths=["8"] ${DAVID_ADDITIONAL_OPTIONS}
    BUILD_COMMAND      ninja
    BUILD_ALWAYS       TRUE
    INSTALL_COMMAND    ""
    TEST_COMMAND       ""
    BUILD_BYPRODUCTS   ${CMAKE_CURRENT_BINARY_DIR}/dav1d-prefix/src/dav1d-build/src/libdav1d.a
  )
  ExternalProject_Get_property(dav1d BINARY_DIR)
  set(DAVID_BINARY_DIR ${BINARY_DIR})
  
  if (EMSCRIPTEN)
    # Variant of dav1d, compiled with auto-vectorization to create WASM-SIMD instructions
    set(DAVID_ADDITIONAL_OPTIONS --cross-file=../../../../../other/dav1d-wasm-cross-simd.txt)
    
    ExternalProject_Add(dav1d-simd
      SOURCE_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d
      CONFIGURE_COMMAND  meson setup ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d --buildtype release --default-library=static -Denable_tests=false -Denable_tools=false -Dbitdepths=["8"] ${DAVID_ADDITIONAL_OPTIONS}
      BUILD_COMMAND      ninja
      BUILD_ALWAYS       TRUE
      INSTALL_COMMAND    ""
      TEST_COMMAND       ""
      BUILD_BYPRODUCTS   ${CMAKE_CURRENT_BINARY_DIR}/dav1d-simd-prefix/src/dav1d-simd-build/src/libdav1d.a
    )
    ExternalProject_Get_property(dav1d-simd BINARY_DIR)
    set(DAVID_SIMD_BINARY_DIR ${BINARY_DIR})
  endif()
  
else()
  # Build for Android
  
  # Since meson requires a cross-file for cross-compilation, but we don't know all of its contents
  # in advance, we have to generate it here with CMake's configure_file().
  set(MESON_SYSTEM "android")
  
  # For the cpu_family setting, see: https://mesonbuild.com/Reference-tables.html#cpu-families
  # Generally, it seems these settings might mainly be used by dav1d's meson.build files rather than
  # by meson itself. So, we can grep these files to find out which values make sense. At the time of
  # writing, it seemed that for ARM, it was either "aarch64" or strings starting with "arm" (there
  # were also mentions of "arm64"). Note that dav1d also comes with example cross files in:
  # dav1d/package/crossfiles/
  # Also, see here for an explanation of what exactly e.g., "aarch64" is:
  # https://stackoverflow.com/a/65918961/2676564
  if (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "arm64-v8a")
    # 64 bit ARM processor
    set(MESON_CPU_FAMILY "aarch64")
    set(MESON_CPU "aarch64")
  elseif (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "armeabi-v7a")
    # 32 bit ARM processor
    set(MESON_CPU_FAMILY "arm")
    set(MESON_CPU "arm")
  elseif (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "x86")
    # 32 bit x86 processor
    set(MESON_CPU_FAMILY "x86")
    set(MESON_CPU "i686")
  elseif (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "x86_64")
    # 64 bit x86 processor
    set(MESON_CPU_FAMILY "x86_64")
    set(MESON_CPU "x86_64")
  else()
    message(FATAL_ERROR "Missing translation from CMAKE_ANDROID_ARCH_ABI ${CMAKE_ANDROID_ARCH_ABI} to MESON_ARCH (defining the cpu_family, cpu entries in the meson cross-file)")
  endif()
  
  set(MESON_C_FLAGS "--target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
  separate_arguments(MESON_C_FLAGS)
  list(JOIN MESON_C_FLAGS "', '" MESON_C_FLAGS)
  
  configure_file(${SCAN_STUDIO_ROOT_PATH}/other/dav1d-cross.meson.in ${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson)
  
  ExternalProject_Add(dav1d
    SOURCE_DIR         ${SCAN_STUDIO_ROOT_PATH}/third_party/dav1d
    CONFIGURE_COMMAND  meson setup ${SCAN_STUDIO_ROOT_PATH}/third_party/dav1d
        --buildtype release --default-library=static -Denable_tests=false -Denable_tools=false -Dbitdepths=["8"]
        --cross-file=${CMAKE_CURRENT_BINARY_DIR}/dav1d-cross-file.meson
    BUILD_COMMAND      ninja
    BUILD_ALWAYS       TRUE
    INSTALL_COMMAND    ""
    TEST_COMMAND       ""
    BUILD_BYPRODUCTS   ${CMAKE_CURRENT_BINARY_DIR}/dav1d-prefix/src/dav1d-build/src/libdav1d.a
    )
  ExternalProject_Get_property(dav1d BINARY_DIR)
  set(DAVID_BINARY_DIR ${BINARY_DIR})
  
endif()
