################################################################################
# CMake file for libvis_io:
# A standalone I/O utility lib (that does not depend on the main libvis library)

cmake_minimum_required(VERSION 3.10.2)

project(libvis_io LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)


################################################################################
# Common Build Settings

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(LIBVIS_IO_OPTIONS
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-pragmas>  # TODO: It seems gcc cannot disable this locally, therefore added it here as a workaround.
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-field-initializers>
  )
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(LIBVIS_OPTIONS
    $<$<COMPILE_LANGUAGE:CXX>:/MP>      # Multi-processor compilation
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    $<$<COMPILE_LANGUAGE:CXX>:/bigobj>
    $<$<COMPILE_LANGUAGE:CXX>:/Zi>      # Debug Information Format: Program Database (for crash reporting)
    $<$<COMPILE_LANGUAGE:CXX>:/DEBUG>   # Generate Debug Info: Yes (for crash reporting)
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/wd4068>
    $<$<COMPILE_LANGUAGE:CXX>:/wd4127>  # https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4127?view=vs-2019
    $<$<COMPILE_LANGUAGE:CXX>:/wd4244>
    $<$<COMPILE_LANGUAGE:CXX>:/wd4267>
    $<$<COMPILE_LANGUAGE:CXX>:/wd4389>
    $<$<COMPILE_LANGUAGE:CXX>:/wd4201>  # nonstandard extension used: nameless struct/union - occurs in glm (as a dependency of gli)
    $<$<COMPILE_LANGUAGE:CXX>:/wd4305>  # 'argument': truncation from 'double' to '(const) float' - very rarely an actual issue
    $<$<COMPILE_LANGUAGE:CXX>:/wd4458>  # "declaration of 'variable' hides class member" - We use that often on purpose e.g., in constructors, explicitly prefixing the class member with "this->" if needed.
    $<$<COMPILE_LANGUAGE:CXX>:/wd4505>  # 'function': unreferenced function with internal linkage has been removed - happens in stb_truetype.h
  )
endif()


################################################################################
# libvis_io

# NOTE: This currently must be STATIC, otherwise the Android Viewer app will not work on old
#       Android devices that would require separate loading of additional shared libraries
#       (see: https://stackoverflow.com/questions/8828559/loading-3rd-party-shared-libraries-from-an-android-native-activity ).
add_library(libvis_io STATIC
  asset_path.cc
  asset_path.h
  filesystem.h
  filesystem_impl.cc
  globals.cc
  globals.h
  input_stream.cc
  input_stream.h
  output_stream.cc
  output_stream.h
)
set_target_properties(libvis_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(libvis_io PUBLIC ${LIBVIS_IO_OPTIONS})
target_include_directories(libvis_io PUBLIC
  ../../  # src
  
  # https://github.com/gulrak/filesystem
  # This is used as a drop-in replacement for std::filesystem if the latter is not supported.
  # Unfortunately, this is still required at the time of writing (October 2022) because
  # we want to support the Android NDK version of the current Unity LTS release, and that does not support it yet.
  ../../../third_party/filesystem/include
)
