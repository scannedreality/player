#pragma once

#include <memory>
#include <string>
#include <vector>

#include "scan_studio/common/common_defines.hpp"

namespace vis {
class InputStream;
class OutputStream;
}

namespace scan_studio {
using namespace vis;

enum class AudioFormat {
  Invalid = 0,
  S8 = 1,
  S16 = 2,
  S24 = 3,
  S32 = 4,
  F32 = 5,
  F64 = 6,
};

#pragma pack(push, 1)
struct WavHeader {
  char riff[4];
  u32 fileSize;
  char wave[4];
  char fmt[4];
  u32 fmtLength;
  u16 fmtType;
  u16 channelCount;
  u32 sampleRate;
  u32 bytesPerSecond;
  u16 bytesPerSampleTimesChannels;
  u16 bitsPerSample;
  char data[4];
  u32 dataSize;
};
#pragma pack(pop)

/// A simple and incomplete class to load basic .wav sound files.
/// * Only supports a single channel (mono) for now.
/// * Assumes that the WAV file follows a 'canonical' form without any extra chunks.
///
/// See here for a brief description of the format:
/// http://soundfile.sapp.org/doc/WaveFormat/
///
/// More details:
/// https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
class WavSound {
 public:
  inline WavSound() {}
  
  WavSound(WavSound&& other) = default;
  WavSound& operator= (WavSound&& other) = default;
  
  WavSound(const WavSound& other) = delete;
  WavSound& operator= (const WavSound& other) = delete;
  
  /// Like Load(), but parses the file header only, without loading the file content.
  /// Returns the number of successfully parsed bytes: 0 in case of failure, the size
  /// of the WAV header in case of success.
  u32 ParseHeader(InputStream* stream, u32* dataSize, u32* sampleRate, int* bytesPerSample);
  
  /// Attempts to load the .wav from the given input stream.
  /// Returns true on success, false on failure.
  bool Load(InputStream* stream);
  
  /// Attempts to save the .wav to the given output stream.
  /// Returns true on success, false on failure.
  bool Save(OutputStream* stream);
  
  /// Attempts to save the external data as .wav to the given output stream.
  /// Returns true on success, false on failure.
  static bool Save(const void* data, usize dataSize, int bytesPerSample, u32 sampleRate, OutputStream* stream);
  
  /// Attempts to save a WAV header to the given output stream.
  /// Returns true on success, false on failure.
  static bool SaveHeader(usize dataSize, int bytesPerSample, u32 sampleRate, OutputStream* stream);
  
  inline int BytesPerSample() const { return bytesPerSample; }
  inline u32 SampleRate() const { return sampleRate; }
  AudioFormat Format() const;
  
  inline usize ComputeSampleCount() const { return data.size() / BytesPerSample(); }
  inline double ComputeDurationInSeconds() const { return ComputeSampleCount() / static_cast<double>(sampleRate); }
  
  inline usize DataSize() const { return data.size(); }
  
  inline const void* Data() const { return data.data(); }
  inline void* Data() { return data.data(); }
  
 private:
  int bytesPerSample;
  u32 sampleRate;
  vector<u8> data;
};

}
