#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>

#include "scan_studio/viewer_common/display_flatscreen.hpp"

using namespace scan_studio;

int main(int argc, char** argv) {
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  
  // For the web viewer, do not print the preamble header,
  // since the program arguments and current working directory are not relevant here.
  // This is done by temporarily setting the stderr verbosity to OFF while calling loguru::init().
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
  loguru::g_preamble_header = false;
  
  loguru::Options options;
  options.verbosity_flag = nullptr;  // don't have loguru parse the program options for the verbosity flag
  options.main_thread_name = nullptr;  // don't have loguru set the thread name
  loguru::init(argc, argv, options);
  
  loguru::g_stderr_verbosity = 2;
  
  const string videoTitle = getenv("VIDEO_TITLE");
  const bool hasAnyHoverInput = getenv("ANY_HOVER_INPUT")[0] == '1';
  
  constexpr bool cacheAllFrames = false;
  constexpr const char* videoPath = "/video.xrv";
  constexpr bool verboseDecoding = false;
  
  constexpr int defaultWindowWidth = 1280;
  constexpr int defaultWindowHeight = 720;
  
  // Pre-reading is used for the web viewer, where at the time of writing this (March 2023),
  // file operations in WASM were a performance problem (but a new "WASMFS" file I/O backend
  // for emscripten was in the works that aims at better performance).
  if (!ShowDesktopViewer(
      RendererType::OpenGL_ES_3_0,
      defaultWindowWidth,
      defaultWindowHeight,
      /*preReadCompleteFile*/ true,
      cacheAllFrames,
      videoPath,
      videoTitle.c_str(),
      /*showMouseControlsHelp*/ hasAnyHoverInput,
      /*showTouchControlsHelp*/ !hasAnyHoverInput,
      /*showTermsAndPrivacyLinks*/ true,
      verboseDecoding,
      /*useVulkanDebugLayers*/ false)) {
    return 1;
  }
  return 0;
}
