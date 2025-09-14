#include "scan_studio/viewer_common/xrvideo/playback_state.hpp"

#include <algorithm>
#include <cmath>

#include <loguru.hpp>

#include "scan_studio/viewer_common/xrvideo/index.hpp"

namespace scan_studio {

void PlaybackState::SetPlaybackConditions(s64 videoStartTime, s64 videoEndTime, PlaybackMode mode, double speed) {
  unique_lock<mutex> lock(accessMutex);
  
  if (this->videoStartTime != videoStartTime ||
      this->videoEndTime != videoEndTime ||
      this->mode != mode ||
      this->playbackSpeed != speed) {
    this->videoStartTime = videoStartTime;
    this->videoEndTime = videoEndTime;
    this->mode = mode;
    this->playbackSpeed = speed;
    
    lock.unlock();
    playbackChangeCondition.notify_all();
  }
}

void PlaybackState::SetPlaybackTimeRange(s64 videoStartTime, s64 videoEndTime) {
  unique_lock<mutex> lock(accessMutex);
  
  if (this->videoStartTime != videoStartTime ||
      this->videoEndTime != videoEndTime) {
    this->videoStartTime = videoStartTime;
    this->videoEndTime = videoEndTime;
    
    lock.unlock();
    playbackChangeCondition.notify_all();
  }
}

void PlaybackState::SetPlaybackMode(PlaybackMode mode) {
  unique_lock<mutex> lock(accessMutex);
  
  if (this->mode != mode) {
    this->mode = mode;
    
    lock.unlock();
    playbackChangeCondition.notify_all();
  }
}

void PlaybackState::SetPlaybackSpeed(double speed) {
  unique_lock<mutex> lock(accessMutex);
  
  if (this->playbackSpeed != speed) {
    this->playbackSpeed = speed;
    
    lock.unlock();
    playbackChangeCondition.notify_all();
  }
}

s64 PlaybackState::Seek(s64 timestamp, bool forward) {
  const bool unlockRequired = accessMutex.try_lock();
  
  timestamp = clamp(timestamp, videoStartTime, videoEndTime);
  
  if (currentTime != timestamp ||
      this->forward != forward) {
    currentTime = timestamp;
    this->forward = forward;
    
    if (unlockRequired) {
      accessMutex.unlock();
    }
    playbackChangeCondition.notify_all();
  } else if (unlockRequired) {
    accessMutex.unlock();
  }
  
  return timestamp;
}

s64 PlaybackState::Advance(s64 elapsedTime) {
  unique_lock<mutex> lock(accessMutex);
  
  const s64 prevTime = currentTime;
  const bool prevForward = forward;
  
  // Note: The static_cast here is necessary to prevent currentTime from being updated
  //       inaccurately via an implicit conversion to double!
  currentTime += static_cast<s64>((forward ? 1 : -1) * round(playbackSpeed * elapsedTime));
  
  if (mode == PlaybackMode::SingleShot) {
    currentTime = clamp(currentTime, videoStartTime, videoEndTime);
  } else if (mode == PlaybackMode::Loop) {
    if (currentTime < videoStartTime || currentTime > videoEndTime) {
      currentTime = videoStartTime + fmod(currentTime - videoStartTime, videoEndTime - videoStartTime);
    }
  } else if (mode == PlaybackMode::BackAndForth) {
    if (currentTime < videoStartTime) {
      currentTime = videoStartTime + (videoStartTime - currentTime);
      forward = true;
    } else if (currentTime > videoEndTime) {
      currentTime = videoEndTime - (currentTime - videoEndTime);
      forward = false;
    }
  }
  
  if (currentTime != prevTime ||
      forward != prevForward) {
    lock.unlock();
    playbackChangeCondition.notify_all();
  }
  
  return currentTime;
}

void PlaybackState::Lock() {
  accessMutex.lock();
}

void PlaybackState::Unlock() {
  accessMutex.unlock();
}

s64 PlaybackState::GetPlaybackTime() const {
  if (accessMutex.try_lock()) { LOG(ERROR) << "The PlaybackState was not locked while calling this function"; }
  
  // TODO: We could extrapolate the playback time here given the current clock time.
  //       Would that be desirable? Or should we stick with the current conservative
  //       way of only returning what has been advanced by calls to Advance() (but
  //       might be slightly out-of-date if it hasn't been called in a while)?
  return currentTime;
}

PlaybackMode PlaybackState::GetPlaybackMode() const {
  if (accessMutex.try_lock()) { LOG(ERROR) << "The PlaybackState was not locked while calling this function"; }
  
  return mode;
}

double PlaybackState::GetPlaybackSpeed() const {
  if (accessMutex.try_lock()) { LOG(ERROR) << "The PlaybackState was not locked while calling this function"; }
  
  return playbackSpeed;
}

bool PlaybackState::PlayingForward() const {
  if (accessMutex.try_lock()) { LOG(ERROR) << "The PlaybackState was not locked while calling this function"; }
  
  return forward;
}


NextFramesIterator::NextFramesIterator(PlaybackState* state, FrameIndex* index)
    : atEnd(false),
      currentFrame(index->FindFrameIndexForTimestamp(state->GetPlaybackTime())),
      forward(state->PlayingForward()),
      mode(state->GetPlaybackMode()),
      index(index) {}

int NextFramesIterator::ComputeDurationToFrame(int frameIndex) const {
  if (frameIndex < 0) { return numeric_limits<int>::max(); }
  if (frameIndex >= index->GetFrameCount()) { return numeric_limits<int>::max(); }
  
  if (forward) {
    // Frame reached in current playback direction?
    if (frameIndex - currentFrame >= 0) {
      return frameIndex - currentFrame;
    }
    
    // Frame not reached by current playback direction
    switch (mode) {
    case PlaybackMode::SingleShot:   return numeric_limits<int>::max();
    case PlaybackMode::Loop:         return index->GetFrameCount() - (currentFrame - frameIndex);
    case PlaybackMode::BackAndForth: return 2 * (index->GetFrameCount() - currentFrame) - 1 + (currentFrame - frameIndex);
    }
  } else {
    // Frame reached in current playback direction?
    if (currentFrame - frameIndex >= 0) {
      return currentFrame - frameIndex;
    }
    
    // Frame not reached by current playback direction
    switch (mode) {
    case PlaybackMode::SingleShot:   return numeric_limits<int>::max();
    case PlaybackMode::Loop:         return index->GetFrameCount() - (frameIndex - currentFrame);
    case PlaybackMode::BackAndForth: return 2 * currentFrame + 1 + (frameIndex - currentFrame);
    }
  }
  
  return -1;
}

int NextFramesIterator::operator*() const {
  return currentFrame;
}

NextFramesIterator& NextFramesIterator::operator++() {
  currentFrame += forward ? 1 : -1;
  
  if (mode == PlaybackMode::SingleShot) {
    if (currentFrame < 0 || currentFrame >= index->GetFrameCount()) {
      atEnd = true;
    }
    currentFrame = clamp(currentFrame, 0, index->GetFrameCount() - 1);
  } else if (mode == PlaybackMode::Loop) {
    if (currentFrame < 0 || currentFrame >= index->GetFrameCount()) {
      currentFrame = (currentFrame + index->GetFrameCount()) % index->GetFrameCount();
    }
  } else if (mode == PlaybackMode::BackAndForth) {
    if (currentFrame < 0) {
      currentFrame = 1;
      forward = true;
    } else if (currentFrame >= index->GetFrameCount()) {
      currentFrame = index->GetFrameCount() - 1;
      forward = false;
    }
  }
  
  return *this;
}

}
