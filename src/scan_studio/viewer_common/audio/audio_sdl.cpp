#include "scan_studio/viewer_common/audio/audio_sdl.hpp"

#include <algorithm>

#include <loguru.hpp>

#ifdef HAVE_SDL
  #include <SDL.h>
#endif

#include <libvis/io/input_stream.h>

#include "scan_studio/common/wav_sound.hpp"  // TODO: Avoid this viewer_common dependency on common

namespace scan_studio {

#ifndef HAVE_SDL

SDLAudio::~SDLAudio() {}

bool SDLAudio::Initialize() { return false; }

void SDLAudio::Destroy() {}

void SDLAudio::SetPlaybackMode(PlaybackMode /*mode*/) {}

void SDLAudio::SetPlaybackPosition(s64 /*nanoseconds*/, bool /*forward*/) {}

bool SDLAudio::TakeAndOpen(InputStream* /*wavStream*/) { return false; }

void SDLAudio::Play() {}

void SDLAudio::Pause() {}

bool SDLAudio::PredictPlaybackTimeAt(chrono::steady_clock::time_point /*timePoint*/, s64* /*predictedPlaybackTimeNanoseconds*/) { return false; }

s64 SDLAudio::SamplesToNanoseconds(s64 /*samples*/) const {
  return 0;
}

s64 SDLAudio::NanosecondsToSamples(s64 /*nanoseconds*/) const {
  return 0;
}

void SDLAudio::AudioCallback(u8* /*stream*/, int /*len*/) {}

void SDLAudio::AudioCallbackStatic(void* /*userdata*/, u8* /*stream*/, int /*len*/) {}

#else

struct SDLAudioImpl {
  SDL_AudioSpec spec;
  
  /// An ID of 0 is invalid.
  SDL_AudioDeviceID deviceId = 0;
};

SDLAudio::~SDLAudio() {
  Destroy();
}

bool SDLAudio::Initialize() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    LOG(ERROR) << "SDL_InitSubSystem(SDL_INIT_AUDIO) failed, SDL_GetError(): " << SDL_GetError();
    return false;
  }
  
  delete impl;
  impl = new SDLAudioImpl();
  return true;
}

void SDLAudio::Destroy() {
  if (impl->deviceId != 0) {
    // Calling this function will wait until the device's audio callback is not running, release the audio hardware and then clean up internal state.
    // No further audio will play from this device once this function returns.
    // This function may block briefly while pending audio data is played by the hardware, so that applications don't drop the last buffer of data they supplied.
    SDL_CloseAudioDevice(impl->deviceId);
  }
  
  delete impl;
  impl = nullptr;
  
  if (wavStream) {
    delete wavStream;
    wavStream = nullptr;
  }
  
  isPlaying = false;
  
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLAudio::SetPlaybackMode(PlaybackMode mode) {
  auto lock = playbackState.Lock();
  lock->mode = mode;
}

void SDLAudio::SetPlaybackPosition(s64 nanoseconds, bool forward) {
  auto lock = playbackState.Lock();
  lock->nextSample = std::max<s64>(0, NanosecondsToSamples(nanoseconds));
  lock->nextSample = std::min((wavSampleCount == 0) ? 0 : (wavSampleCount - 1), lock->nextSample);
  lock->forward = forward;
}

s64 SDLAudio::GetPlaybackPosition() {
  auto lock = playbackState.Lock();
  return SamplesToNanoseconds(lock->nextSample);
}

bool SDLAudio::TakeAndOpen(InputStream* wavStream) {
  if (this->wavStream) {
    Destroy();
    if (!Initialize()) { return false; }
  }
  
  // Take ownership of wavStream
  this->wavStream = wavStream;
  
  // Reset the playback state to the start
  {
    auto lock = playbackState.Lock();
    lock->nextSample = 0;
    lock->forward = true;
  }
  
  // Parse the WAV header
  WavSound wav;
  u32 wavDataSize;
  u32 wavSampleRate;
  wavHeaderSize = wav.ParseHeader(wavStream, &wavDataSize, &wavSampleRate, &wavBytesPerSample);
  if (wavHeaderSize == 0) {
    return false;
  }
  
  wavSampleCount = wavDataSize / wavBytesPerSample;
  
  // Open the SDL audio device
  SDL_AudioSpec desiredSpec;
  SDL_memset(&desiredSpec, 0, sizeof(desiredSpec));
  
  desiredSpec.freq = wavSampleRate;
  if (wavBytesPerSample == 1) {
    desiredSpec.format = AUDIO_S8;
  } else if (wavBytesPerSample == 2) {
    desiredSpec.format = AUDIO_S16;
  } else if (wavBytesPerSample == 4) {
    desiredSpec.format = AUDIO_S32;
  } else {
    LOG(ERROR) << "Unsupported number of bytes per sample in WAV file: " << wavBytesPerSample;
    return false;
  }
  desiredSpec.channels = 1;
  desiredSpec.samples = 4096;
  desiredSpec.callback = &SDLAudio::AudioCallbackStatic;
  desiredSpec.userdata = this;
  
  // Use the default audio device by passing null for the device.
  // Since we only play the single WAV file that is given here (and no other sounds), for convenience we simply initialize the audio device
  // to the same settings as the WAV file and let SDL handle any required conversion internally by setting allowed_changes = 0.
  impl->deviceId = SDL_OpenAudioDevice(/*device*/ nullptr, /*iscapture*/ 0, &desiredSpec, &impl->spec, /*allowed_changes*/ 0);
  if (impl->deviceId == 0) {
    LOG(ERROR) << "SDL_OpenAudioDevice() failed, SDL_GetError(): " << SDL_GetError();
    return false;
  }
  
  // Note: An opened SDL audio device starts out paused,
  // and should be enabled for playing by calling SDL_PauseAudioDevice(devid, 0).
  
  return true;
}

void SDLAudio::Play() {
  if (isPlaying) { return; }
  
  // Reset time sync
  {
    auto lock = timeSync.Lock();
    lock->totalPlayedSamples = 0;
    // NOTE: To prevent deadlocks, never lock the mutexes in the other order
    {
      auto playbackStateLock = playbackState.Lock();
      lock->samplePlaybackPosition = playbackStateLock->nextSample;
    }
    lock->playedSamplesAtTimePointPairs.clear();
  }
  
  SDL_PauseAudioDevice(impl->deviceId, /*pause_on*/ 0);
  isPlaying = true;
}

void SDLAudio::Pause() {
  if (!isPlaying) { return; }
  
  // Note: Pausing state does not stack; even if you pause a device several times,
  // a single unpause will start the device playing again, and vice versa.
  SDL_PauseAudioDevice(impl->deviceId, /*pause_on*/ 1);
  isPlaying = false;
}

bool SDLAudio::PredictPlaybackTimeAt(chrono::steady_clock::time_point timePoint, s64* predictedPlaybackTimeNanoseconds) {
  TimeSync timeSyncCopy;
  {
    auto lock = timeSync.Lock();
    timeSyncCopy = *lock;
  }
  
  if (timeSyncCopy.playedSamplesAtTimePointPairs.empty()) {
    return false;
  }
  
  PlaybackMode mode;
  bool forward;
  {
    auto lock = playbackState.Lock();
    mode = lock->mode;
    forward = lock->forward;
  }
  
  // Using playedSamplesAtTimePointPairs, predict the number of played samples at the given timePoint.
  // Assuming that chrono::steady_clock and the audio clock advance at the same rate,
  // we only need to optimize for an offset between the clocks by simply taking the median sample offset.
  vector<s64> clockOffsets(timeSyncCopy.playedSamplesAtTimePointPairs.size());
  for (int i = 0; i < clockOffsets.size(); ++ i) {
    const auto& samplePair = timeSyncCopy.playedSamplesAtTimePointPairs[i];
    clockOffsets[i] = SamplesToNanoseconds(samplePair.first) - samplePair.second.time_since_epoch().count();
  }
  
  const auto medianClockOffsetIt = clockOffsets.begin() + (clockOffsets.size() / 2);
  nth_element(clockOffsets.begin(), medianClockOffsetIt, clockOffsets.end());
  
  const s64 predictedPlayedSamples = NanosecondsToSamples(timePoint.time_since_epoch().count() + *medianClockOffsetIt);
  
  // From the predicted number of played samples, compute the playback time
  // (virtually advancing from timeSyncCopy.totalPlayedSamples and timeSyncCopy.samplePlaybackPosition)
  const s64 samplesToAdvance = (forward ? 1 : -1) * (predictedPlayedSamples - timeSyncCopy.totalPlayedSamples);
  
  // LOG(1) << "timeSyncCopy.totalPlayedSamples: " << timeSyncCopy.totalPlayedSamples;
  // LOG(1) << "predictedPlayedSamples: " << predictedPlayedSamples;
  // LOG(1) << "samplesToAdvance: " << samplesToAdvance;
  
  if (mode == PlaybackMode::SingleShot) {
    *predictedPlaybackTimeNanoseconds = SamplesToNanoseconds(std::max<s64>(0, std::min<s64>(wavSampleCount - 1, timeSyncCopy.samplePlaybackPosition + samplesToAdvance)));
    return true;
  } else if (mode == PlaybackMode::Loop) {
    *predictedPlaybackTimeNanoseconds = SamplesToNanoseconds((timeSyncCopy.samplePlaybackPosition + samplesToAdvance) % wavSampleCount);
    return true;
  } else if (mode == PlaybackMode::BackAndForth) {
    s64 predictedSamplePlaybackPosition = timeSyncCopy.samplePlaybackPosition + samplesToAdvance;
    s64 interval;
    if (predictedSamplePlaybackPosition < 0) {
      interval = (predictedSamplePlaybackPosition / wavSampleCount) - 1;
    } else {
      interval = predictedSamplePlaybackPosition / wavSampleCount;
    }
    predictedSamplePlaybackPosition -= interval * wavSampleCount;
    if ((interval & 1) == 1) {
      predictedSamplePlaybackPosition = wavSampleCount - 1 - predictedSamplePlaybackPosition;
    }
    *predictedPlaybackTimeNanoseconds = SamplesToNanoseconds(predictedSamplePlaybackPosition);
    return true;
  }
  
  LOG(ERROR) << "Unsupported playback mode: " << static_cast<int>(mode);
  return false;
}

s64 SDLAudio::SamplesToNanoseconds(s64 samples) const {
  return (samples * static_cast<s64>(1000 * 1000 * 1000)) / static_cast<s64>(impl->spec.freq);
}

s64 SDLAudio::NanosecondsToSamples(s64 nanoseconds) const {
  return (static_cast<s64>(impl->spec.freq) * nanoseconds + static_cast<s64>(500 * 1000 * 1000)) / static_cast<s64>(1000 * 1000 * 1000);
}

void SDLAudio::AudioCallback(u8* stream, int len) {
  // Update time synchronization.
  // Since we do not know about the hardware output delay, as a simplifying assumption,
  // we assume that the buffer being written here is output without delay.
  // (Old: with a delay of one audio buffer starting from the time at which this callback is called.)
  constexpr int kMaxNumAudioTimingOffsetSamples = 21;
  
  const int bufferSamples = len / wavBytesPerSample;
  const chrono::steady_clock::time_point outputTime =
      chrono::steady_clock::now(); // +
      // chrono::duration<s64, nano>(SamplesToNanoseconds(bufferSamples));
  
  {
    auto lock = timeSync.Lock();
    lock->playedSamplesAtTimePointPairs.emplace_back(lock->totalPlayedSamples, outputTime);
    while (lock->playedSamplesAtTimePointPairs.size() > kMaxNumAudioTimingOffsetSamples) {
      lock->playedSamplesAtTimePointPairs.erase(lock->playedSamplesAtTimePointPairs.begin());
    }
    lock->totalPlayedSamples += bufferSamples;
    // NOTE: To prevent deadlocks, never lock the mutexes in the other order
    {
      auto playbackStateLock = playbackState.Lock();
      lock->samplePlaybackPosition = playbackStateLock->nextSample;
    }
  }
  
  // Write to the output buffer
  while (len > 0) {
    // Get the current playback position and advance it
    bool emitSilence = false;
    bool playForward;
    s64 nextSample;
    int readLen;
    {
      auto lock = playbackState.Lock();
      
      s64 remainingSamples = lock->forward ? (wavSampleCount - lock->nextSample) : lock->nextSample;
      
      if (remainingSamples == 0) {
        // Reached the end of the audio.
        if (wavSampleCount == 0 || lock->mode == PlaybackMode::SingleShot) {
          emitSilence = true;
          readLen = len;
        } else if (lock->mode == PlaybackMode::Loop) {
          if (!wavStream->Seek(wavHeaderSize)) {
            LOG(ERROR) << "Seeking in audio stream failed";
          }
          lock->nextSample = 0;
          lock->forward = true;
          remainingSamples = wavSampleCount;
        } else if (lock->mode == PlaybackMode::BackAndForth) {
          lock->forward = !lock->forward;
          remainingSamples = wavSampleCount;
        } else {
          LOG(ERROR) << "Unsupported playback mode: " << static_cast<int>(lock->mode);
        }
      }
      
      nextSample = lock->nextSample;
      playForward = lock->forward;
      
      if (!emitSilence) {
        readLen = std::min<s64>(remainingSamples * wavBytesPerSample, len);
        lock->nextSample += (playForward ? 1 : -1) * (readLen / wavBytesPerSample);
      }
    }
    
    if (emitSilence) {
      SDL_memset(stream, impl->spec.silence, readLen);
    } else if (playForward) {
      // TODO: Do we need a separate I/O thread to smooth over hiccups on slow disks (HDDs)?
      //       (However, consider that we plan to integrate the audio with .xrv files in the future anyway,
      //        which will give us a separate I/O thread; so we probably don't need to address this beforehand.)
      if (!wavStream->Seek(wavHeaderSize + nextSample * wavBytesPerSample)) {
        LOG(ERROR) << "Seeking in audio stream failed";
      }
      if (!wavStream->ReadFully(stream, readLen)) {
        LOG(ERROR) << "Error reading from the audio stream";
      }
    } else {
      if (!wavStream->Seek(wavHeaderSize + nextSample * wavBytesPerSample - readLen)) {
        LOG(ERROR) << "Seeking in audio stream failed";
      }
      
      if (!wavStream->ReadFully(stream, readLen)) {
        LOG(ERROR) << "Error reading from the audio stream";
      }
      
      for (int i = 0; i < readLen / 2; ++ i) {
        std::swap(stream[i], stream[readLen - 1 - i]);
      }
    }
    
    stream += readLen;
    len -= readLen;
  }
}

void SDLAudio::AudioCallbackStatic(void* userdata, u8* stream, int len) {
  SDLAudio* self = static_cast<SDLAudio*>(userdata);
  self->AudioCallback(stream, len);
}

#endif

}
