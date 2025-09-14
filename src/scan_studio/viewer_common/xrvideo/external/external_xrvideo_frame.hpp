#pragma once

#include <vector>

#include "scan_studio/viewer_common/xrvideo/frame_loading.hpp"
#include "scan_studio/viewer_common/xrvideo/xrvideo_frame.hpp"

#include "scan_studio/player_library/scannedreality_player.h"

namespace scan_studio {
using namespace vis;

class ExternalXRVideo;
class TextureFramePromise;

/// Represents a single frame of an XRVideo, for passing the decoded data to external callbacks.
class ExternalXRVideoFrame : public XRVideoFrame {
 public:
  inline ExternalXRVideoFrame() {}
  ~ExternalXRVideoFrame();
  
  inline ExternalXRVideoFrame(ExternalXRVideoFrame&& other) = default;
  inline ExternalXRVideoFrame& operator= (ExternalXRVideoFrame&& other) = default;
  
  inline ExternalXRVideoFrame(const ExternalXRVideoFrame& other) = delete;
  inline ExternalXRVideoFrame& operator= (const ExternalXRVideoFrame& other) = delete;
  
  void Configure(const ExternalXRVideo* xrVideo);
  
  bool Initialize(
      const XRVideoFrameMetadata& metadata,
      const u8* contentPtr,
      TextureFramePromise* textureFramePromise,
      XRVideoDecodingContext* decodingContext,
      bool verboseDecoding);
  void Destroy();
  
  void WaitForResourceTransfers();
  
  /// Returns the externally provided frame user data.
  inline void* UserData() const { return frameUserData; }
  
 private:
  SRPlayer_XRVideo_Frame_Metadata frameMetadataForAPI;
  
  void* frameUserData;
  const ExternalXRVideo* xrVideo;
};

}
