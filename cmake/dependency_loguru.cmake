add_library(loguru STATIC
  ${SCAN_STUDIO_ROOT_PATH}/third_party/libvis/third_party/loguru/loguru.cpp
)
target_include_directories(loguru PUBLIC
  ${SCAN_STUDIO_ROOT_PATH}/third_party/libvis/third_party/loguru/
)
target_compile_definitions(loguru PUBLIC
  LOGURU_WITH_STREAMS
  LOGURU_REPLACE_GLOG
)
target_link_libraries(loguru PRIVATE
  ${CMAKE_DL_LIBS}  # name of library containing dlopen and dlclose
)
if (NOT EMSCRIPTEN)
  # Cross-platform threading:
  # https://cmake.org/cmake/help/latest/module/FindThreads.html
  find_package(Threads REQUIRED)
  target_link_libraries(loguru PRIVATE Threads::Threads)
endif()
set_target_properties(loguru PROPERTIES POSITION_INDEPENDENT_CODE ON)

# In emscripten, compile with pthread support
if (EMSCRIPTEN)
  target_compile_options(loguru PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-pthread>)
  target_link_options(loguru PUBLIC -pthread)
endif()

# In MSVC, try to fix DLL import / export
if(MSVC)
  set(LOGURU_DLL_EXPORT "__declspec(dllexport)")
  #set(LOGURU_DLL_IMPORT "__declspec(dllimport)")
endif()
target_compile_definitions(loguru PRIVATE
  LOGURU_EXPORT=${LOGURU_DLL_EXPORT}
)
