#pragma once

#include <condition_variable>
#include <mutex>

#include <libvis/vulkan/libvis.h>

namespace scan_studio {
using namespace vis;

class FrameIndex;

/// Important: The numerical values in this enum are used in external code (Javascript API, Unity plugin C# code, C API, ...).
///            Do not change them!
enum class PlaybackMode {
  /// Plays the video once and then shows the final frame statically
  SingleShot = 0,
  
  /// Loops the video
  Loop = 1,
  
  /// Alternatingly plays the video forwards and backwards,
  /// creating an infinitely looped video without discontinuity
  BackAndForth = 2,
};

/// Represents the current playback time and settings (e.g., whether to loop the video)
/// as a shared state that is accessed by both the rendering and the decoding thread.
///
/// TODO: We could also implement the logic here that avoids render times coinciding
///       with frame boundaries by nudging the playback time slightly if that happens
///       for a few frames in a row
class PlaybackState {
 public:
  /// Sets the playback conditions. To be called when a new video is started, or when the
  /// user changes the playback mode.
  void SetPlaybackConditions(s64 videoStartTime, s64 videoEndTime, PlaybackMode mode, double speed);
  
  /// Sets the playback time range.
  void SetPlaybackTimeRange(s64 videoStartTime, s64 videoEndTime);
  
  /// Sets the playback mode.
  void SetPlaybackMode(PlaybackMode mode);
  
  /// Sets the playback speed.
  void SetPlaybackSpeed(double speed);
  
  /// Changes the current playback time to the given video timestamp, and sets the
  /// forward/backward play state. Holding the PlaybackState lock while calling Seek() is optional.
  /// Returns the time after seeking (which may differ from the passed in time if it was clamped).
  s64 Seek(s64 timestamp, bool forward);
  
  /// Advances the playback corresponding to the given elapsed time. Note that for example
  /// if the video is set to slow motion, the playback time might change by less than the given
  /// elapsed time. Returns the resulting playback time.
  s64 Advance(s64 elapsedTime);
  
  /// Locks the playback state.
  void Lock();
  
  /// Unlocks the playback state.
  void Unlock();
  
  /// Gets the current video playback time.
  /// Attention: The PlaybackState must be locked when this is called.
  s64 GetPlaybackTime() const;
  
  /// Gets the current video playback mode.
  /// Attention: The PlaybackState must be locked when this is called.
  PlaybackMode GetPlaybackMode() const;
  
  /// Gets the current video playback speed.
  /// Attention: The PlaybackState must be locked when this is called.
  double GetPlaybackSpeed() const;
  
  /// Returns true if the video is currently playing forward,
  /// false if the video is currently playing backward.
  /// Attention: The PlaybackState must be locked when this is called.
  bool PlayingForward() const;
  
  /// Returns the PlaybackState's access mutex, such that it can be locked
  /// with a unique_lock.
  inline mutex& GetMutex() { return accessMutex; }
  
  /// Returns a condition_variable that gets notified when there is any change in the playback:
  /// * Advance() changed the current time
  /// * Seek() changed the current time
  /// * SetPlaybackConditions() changed anything
  inline condition_variable& GetPlaybackChangeCondition() { return playbackChangeCondition; }
  
 private:
  mutable mutex accessMutex;
  
  condition_variable playbackChangeCondition;
  
  /// The current time of playback
  s64 currentTime = numeric_limits<s64>::lowest();
  
  /// Whether playback currently runs forward or backward.
  bool forward = true;
  
  /// A factor on the playback speed.
  double playbackSpeed = 1;
  
  /// The playback conditions set by SetPlaybackConditions().
  s64 videoStartTime = numeric_limits<s64>::lowest();
  s64 videoEndTime = numeric_limits<s64>::lowest();
  PlaybackMode mode = PlaybackMode::SingleShot;
};

/// Using a PlaybackState and a FrameIndex, this iterator returns the next frame indices
/// that will be played back (assuming that the user will not influence playback, e.g., by seeking).
/// This is used by the decoding thread to select the next frame for decoding.
class NextFramesIterator {
 public:
  /// Constructs a NextFramesIterator at the given state's current frame.
  /// Attention: The PlaybackState must be locked when using this constructor.
  NextFramesIterator(PlaybackState* state, FrameIndex* index);
  
  /// Returns true if the last ++ operation had no effect since the end of the iterator was reached.
  inline bool AtEnd() const { return atEnd; }
  
  /// Computes the duration from the iterator's current frame to the given frame index.
  /// The duration is measured in the number of frames until reaching the given frame.
  /// For example, if the iterator is at frame 2 and forward playback is used, then
  /// ComputeDurationToFrame(5) returns 3. If the frame will never be played back (again),
  /// returns numeric_limits<int>::max(). If frameIndex is equal to the currently displayed
  /// frame, returns 0.
  int ComputeDurationToFrame(int frameIndex) const;
  
  /// Returns the iterator's current frame.
  int operator*() const;
  
  /// Proceeds to the next frame that will be played back.
  NextFramesIterator& operator++();
  
 private:
  bool atEnd;
  int currentFrame;
  
  bool forward;
  PlaybackMode mode;
  
  FrameIndex* index;  // not owned
};

}

