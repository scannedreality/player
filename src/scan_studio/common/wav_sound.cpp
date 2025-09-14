#include "scan_studio/common/wav_sound.hpp"

#include <cstring>

#include <loguru.hpp>

#include <libvis/io/input_stream.h>
#include <libvis/io/output_stream.h>

namespace scan_studio {

u32 WavSound::ParseHeader(InputStream* stream, u32* dataSize, u32* sampleRate, int* bytesPerSample) {
  WavHeader header;
  if (!stream->ReadFully(&header, sizeof(header))) {
    return 0;
  }
  
  if (memcmp(header.riff, "RIFF", 4) != 0) {
    LOG(ERROR) << "The given input does not seem to be a WAV file: Header does not start with RIFF";
    return 0;
  }
  
  if (memcmp(header.wave, "WAVE", 4) != 0) {
    LOG(ERROR) << "The given input does not seem to be a WAV file: Expected WAVE chunk not found";
    return 0;
  }
  
  if (memcmp(header.fmt, "fmt ", 4) != 0) {
    LOG(ERROR) << "The given input does not seem to be a WAV file: Expected fmt chunk not found";
    return 0;
  }
  
  if (header.fmtLength != 16) {
    LOG(ERROR) << "Failed to parse the given WAV file: fmt chunk does not have expected length 16 (actual length: " << header.fmtLength << ")";
    return 0;
  }
  
  if (header.fmtType != 1) {
    LOG(ERROR) << "Failed to parse the given WAV file: Unexpected fmtType: " << header.fmtType;
    return 0;
  }
  
  if (header.channelCount != 1) {
    LOG(ERROR) << "Failed to parse the given WAV file: Unsupported channelCount, only mono is supported at the moment: " << header.channelCount;
    return 0;
  }
  
  if (memcmp(header.data, "data", 4) != 0) {
    LOG(ERROR) << "The given input does not seem to be a WAV file: Expected data chunk not found";
    return 0;
  }
  
  *dataSize = header.dataSize;
  *sampleRate = header.sampleRate;
  *bytesPerSample = header.bitsPerSample / 8;
  return sizeof(WavHeader);
}

bool WavSound::Load(InputStream* stream) {
  u32 dataSize;
  if (ParseHeader(stream, &dataSize, &sampleRate, &bytesPerSample) == 0) {
    return false;
  }
  
  data.resize(dataSize);
  if (!stream->ReadFully(data.data(), dataSize)) {
    LOG(ERROR) << "Failed to parse the given WAV file: Could not read all data bytes (perhaps the file header's dataSize is incorrect?)";
    return false;
  }
  
  return true;
}

bool WavSound::Save(OutputStream* stream) {
  return WavSound::Save(data.data(), data.size(), bytesPerSample, sampleRate, stream);
}

bool WavSound::Save(const void* data, usize dataSize, int bytesPerSample, u32 sampleRate, OutputStream* stream) {
  if (!SaveHeader(dataSize, bytesPerSample, sampleRate, stream)) {
    return false;
  }
  
  if (!stream->WriteFully(data, dataSize)) {
    LOG(ERROR) << "Failed to write WAV data";
    return false;
  }
  
  return true;
}

bool WavSound::SaveHeader(usize dataSize, int bytesPerSample, u32 sampleRate, OutputStream* stream) {
  constexpr int channelCount = 1;
  
  WavHeader header;
  header.riff[0] = 'R';
  header.riff[1] = 'I';
  header.riff[2] = 'F';
  header.riff[3] = 'F';
  header.fileSize = sizeof(header) + dataSize - 8;
  header.wave[0] = 'W';
  header.wave[1] = 'A';
  header.wave[2] = 'V';
  header.wave[3] = 'E';
  header.fmt[0] = 'f';
  header.fmt[1] = 'm';
  header.fmt[2] = 't';
  header.fmt[3] = ' ';
  header.fmtLength = 16;
  header.fmtType = 1;
  header.channelCount = channelCount;
  header.sampleRate = sampleRate;
  header.bytesPerSecond = sampleRate * channelCount * bytesPerSample;
  header.bytesPerSampleTimesChannels = channelCount * bytesPerSample;
  header.bitsPerSample = 8 * bytesPerSample;
  header.data[0] = 'd';
  header.data[1] = 'a';
  header.data[2] = 't';
  header.data[3] = 'a';
  header.dataSize = dataSize;
  
  if (!stream->WriteFully(&header, sizeof(header))) {
    LOG(ERROR) << "Failed to write WAV header";
    return false;
  }
  
  return true;
}

AudioFormat WavSound::Format() const {
  if (bytesPerSample == 1) {
    // TODO: For 8-bit data, the sample format in WAV is U8 (unsigned 8-bit), but we currently don't have an entry for that in the AudioFormat enum!
    return AudioFormat::Invalid;
  } else if (bytesPerSample == 2) {
    return AudioFormat::S16;
  } else if (bytesPerSample == 3) {
    return AudioFormat::S24;
  } else if (bytesPerSample == 4) {
    return AudioFormat::S32;
  }
  
  return AudioFormat::Invalid;
}

}
