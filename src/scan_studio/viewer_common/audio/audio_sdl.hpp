#pragma once

#include <chrono>
#include <vector>

#include <libvis/vulkan/libvis.h>

#include "scan_studio/common/wrap_mutex.hpp"

#include "scan_studio/viewer_common/xrvideo/playback_state.hpp"

namespace vis {
class InputStream;
}

namespace scan_studio {
using namespace vis;

struct SDLAudioImpl;

class SDLAudio {
 public:
  ~SDLAudio();
  
  bool Initialize();
  void Destroy();
  
  void SetPlaybackMode(PlaybackMode mode);
  
  void SetPlaybackPosition(s64 nanoseconds, bool forward);
  
  /// Returns the current playback position in nanoseconds from the start of the audio.
  s64 GetPlaybackPosition();
  
  /// Opens the WAV file from the given input stream, taking ownership of the input stream.
  /// Does not start playing; to do that, call Play() afterwards.
  bool TakeAndOpen(InputStream* wavStream);
  
  void Play();
  void Pause();
  
  /// For video-audio time synchronization: Predicts the audio playback time
  /// at the given chrono::steady_clock::time_point. This function may change
  /// its prediction abrubtly as new data comes in; the user is responsible
  /// for smoothing the estimates if needed. Returns true on success, false
  /// if insufficient data is available to make the prediction.
  bool PredictPlaybackTimeAt(chrono::steady_clock::time_point timePoint, s64* predictedPlaybackTimeNanoseconds);
  
  inline bool IsPlaying() const { return isPlaying; }
  
  s64 SamplesToNanoseconds(s64 samples) const;
  s64 NanosecondsToSamples(s64 nanoseconds) const;
  
 private:
  void AudioCallback(u8* stream, int len);
  static void AudioCallbackStatic(void* userdata, u8* stream, int len);
  
  // Playback state
  struct PlaybackState {
    PlaybackMode mode = PlaybackMode::SingleShot;
    s64 nextSample;
    bool forward;
  };
  WrapMutex<PlaybackState> playbackState;
  bool isPlaying = false;
  
  // Time synchronization
  struct TimeSync {
    s64 totalPlayedSamples;
    s64 samplePlaybackPosition;
    vector<pair<s64, chrono::steady_clock::time_point>> playedSamplesAtTimePointPairs;
  };
  WrapMutex<TimeSync> timeSync;
  
  // Input
  u32 wavHeaderSize;
  s64 wavSampleCount;
  int wavBytesPerSample;
  InputStream* wavStream = nullptr;
  
  // Impl (to avoid #including an SDL header here)
  SDLAudioImpl* impl = nullptr;
};

}
