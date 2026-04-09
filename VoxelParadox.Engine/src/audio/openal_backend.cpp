#include "audio/openal_backend.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>
#include <system_error>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace ENGINE {
namespace AUDIO {
namespace {

using Microsoft::WRL::ComPtr;

struct RiffChunkHeader {
  char id[4];
  std::uint32_t size = 0;
};

struct WavDecodeInfo {
  PcmFormatInfo format{};
  std::uint32_t dataOffset = 0;
  std::uint32_t dataSize = 0;
};

class ScopedComInitialization {
public:
  bool initialize(std::string& outError) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    result_ = hr;

    if (hr == S_OK || hr == S_FALSE) {
      uninitializeOnDestroy_ = true;
      return true;
    }

    if (hr == RPC_E_CHANGED_MODE) {
      return true;
    }

    outError = "Failed to initialize COM for MP3 decoding.";
    return false;
  }

  ~ScopedComInitialization() {
    if (uninitializeOnDestroy_) {
      CoUninitialize();
    }
  }

private:
  HRESULT result_ = E_FAIL;
  bool uninitializeOnDestroy_ = false;
};

bool readBytes(std::ifstream& file, void* destination, std::size_t size) {
  file.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
  return static_cast<std::size_t>(file.gcount()) == size;
}

ALenum alFormatFor(std::uint16_t channels, std::uint16_t bitsPerSample) {
  if (channels == 1 && bitsPerSample == 8) {
    return AL_FORMAT_MONO8;
  }
  if (channels == 1 && bitsPerSample == 16) {
    return AL_FORMAT_MONO16;
  }
  if (channels == 2 && bitsPerSample == 8) {
    return AL_FORMAT_STEREO8;
  }
  if (channels == 2 && bitsPerSample == 16) {
    return AL_FORMAT_STEREO16;
  }
  return 0;
}

std::string lowerExtension(const std::filesystem::path& path) {
  std::string value = path.extension().string();
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

double computeDurationSeconds(const PcmFormatInfo& format, std::size_t byteCount) {
  if (format.sampleRate == 0u || format.bytesPerFrame == 0u) {
    return 0.0;
  }

  return static_cast<double>(byteCount) /
         static_cast<double>(format.sampleRate * format.bytesPerFrame);
}

bool parseWavHeader(std::ifstream& file, WavDecodeInfo& outInfo,
                    std::string& outError) {
  file.seekg(0, std::ios::beg);

  std::array<char, 4> riffId{};
  std::uint32_t riffSize = 0;
  std::array<char, 4> waveId{};
  if (!readBytes(file, riffId.data(), riffId.size()) ||
      !readBytes(file, &riffSize, sizeof(riffSize)) ||
      !readBytes(file, waveId.data(), waveId.size())) {
    outError = "WAV file is too small.";
    return false;
  }

  if (std::string(riffId.data(), riffId.size()) != "RIFF" ||
      std::string(waveId.data(), waveId.size()) != "WAVE") {
    outError = "File is not a RIFF/WAVE stream.";
    return false;
  }

  bool foundFormat = false;
  bool foundData = false;
  std::uint16_t audioFormat = 0;

  while (file.good() && !(foundFormat && foundData)) {
    RiffChunkHeader chunk{};
    if (!readBytes(file, &chunk, sizeof(chunk))) {
      break;
    }

    const std::streampos chunkDataOffset = file.tellg();
    const std::string chunkId(chunk.id, 4);
    if (chunkId == "fmt ") {
      struct WaveFormatChunk {
        std::uint16_t audioFormat;
        std::uint16_t channels;
        std::uint32_t sampleRate;
        std::uint32_t byteRate;
        std::uint16_t blockAlign;
        std::uint16_t bitsPerSample;
      } formatChunk{};

      if (chunk.size < sizeof(formatChunk) ||
          !readBytes(file, &formatChunk, sizeof(formatChunk))) {
        outError = "Invalid WAV fmt chunk.";
        return false;
      }

      audioFormat = formatChunk.audioFormat;
      outInfo.format.channels = formatChunk.channels;
      outInfo.format.bitsPerSample = formatChunk.bitsPerSample;
      outInfo.format.sampleRate = formatChunk.sampleRate;
      outInfo.format.bytesPerFrame =
          static_cast<std::uint32_t>(formatChunk.channels) *
          static_cast<std::uint32_t>(formatChunk.bitsPerSample / 8u);
      outInfo.format.alFormat =
          alFormatFor(formatChunk.channels, formatChunk.bitsPerSample);

      if (outInfo.format.alFormat == 0) {
        outError = "Unsupported WAV channel/bit-depth combination.";
        return false;
      }
      foundFormat = true;
    } else if (chunkId == "data") {
      outInfo.dataOffset =
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(chunkDataOffset));
      outInfo.dataSize = chunk.size;
      foundData = true;
    }

    std::uint32_t paddedSize = chunk.size;
    if ((paddedSize & 1u) != 0u) {
      paddedSize += 1u;
    }
    file.seekg(chunkDataOffset + static_cast<std::streamoff>(paddedSize));
  }

  if (!foundFormat || !foundData) {
    outError = "WAV file is missing fmt or data chunk.";
    return false;
  }
  if (audioFormat != 1u) {
    outError = "Only PCM WAV files are supported.";
    return false;
  }
  if (outInfo.format.bytesPerFrame == 0u || outInfo.format.sampleRate == 0u) {
    outError = "Invalid WAV format values.";
    return false;
  }

  return true;
}

bool decodeWavFile(const std::filesystem::path& path, DecodedAudioData& outData,
                   std::string& outError) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    outError = "Failed to open WAV file: " + path.string();
    return false;
  }

  WavDecodeInfo info{};
  if (!parseWavHeader(file, info, outError)) {
    outError += " (" + path.string() + ")";
    return false;
  }

  outData.format = info.format;
  outData.pcmData.resize(info.dataSize);
  outData.durationSeconds = computeDurationSeconds(info.format, outData.pcmData.size());

  file.seekg(static_cast<std::streamoff>(info.dataOffset), std::ios::beg);
  if (!readBytes(file, outData.pcmData.data(), outData.pcmData.size())) {
    outError = "Failed to read PCM data from WAV file: " + path.string();
    outData = {};
    return false;
  }

  return true;
}

bool decodeOggFile(const std::filesystem::path& path, DecodedAudioData& outData,
                   std::string& outError) {
  int channels = 0;
  int sampleRate = 0;
  short* samples = nullptr;

  const int samplesPerChannel =
      stb_vorbis_decode_filename(path.string().c_str(), &channels, &sampleRate, &samples);
  if (samplesPerChannel < 0 || !samples) {
    outError = "Failed to decode OGG file: " + path.string();
    return false;
  }

  const ALenum alFormat = alFormatFor(static_cast<std::uint16_t>(channels), 16u);
  if (alFormat == 0) {
    std::free(samples);
    outError = "Unsupported OGG channel layout: " + path.string();
    return false;
  }

  outData.format.alFormat = alFormat;
  outData.format.channels = static_cast<std::uint16_t>(channels);
  outData.format.bitsPerSample = 16;
  outData.format.sampleRate = static_cast<std::uint32_t>(sampleRate);
  outData.format.bytesPerFrame =
      static_cast<std::uint32_t>(channels) * static_cast<std::uint32_t>(sizeof(short));

  const std::size_t totalSampleCount =
      static_cast<std::size_t>(samplesPerChannel) * static_cast<std::size_t>(channels);
  outData.pcmData.resize(totalSampleCount * sizeof(short));
  std::memcpy(outData.pcmData.data(), samples, outData.pcmData.size());
  outData.durationSeconds =
      sampleRate > 0 ? static_cast<double>(samplesPerChannel) / static_cast<double>(sampleRate)
                     : 0.0;

  std::free(samples);
  return true;
}

bool ensureMediaFoundation(std::string& outError) {
  static std::once_flag once;
  static HRESULT mfStartupResult = E_FAIL;

  std::call_once(once, [] {
    mfStartupResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
  });

  if (FAILED(mfStartupResult)) {
    outError = "Failed to initialize Media Foundation for MP3 decoding.";
    return false;
  }

  return true;
}

bool decodeMp3File(const std::filesystem::path& path, DecodedAudioData& outData,
                   std::string& outError) {
  ScopedComInitialization comScope;
  if (!comScope.initialize(outError)) {
    return false;
  }

  if (!ensureMediaFoundation(outError)) {
    return false;
  }

  ComPtr<IMFSourceReader> reader;
  const std::wstring widePath = path.wstring();
  HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader);
  if (FAILED(hr) || !reader) {
    outError = "Failed to open MP3 file: " + path.string();
    return false;
  }

  ComPtr<IMFMediaType> pcmType;
  hr = MFCreateMediaType(&pcmType);
  if (FAILED(hr)) {
    outError = "Failed to create Media Foundation type for MP3 decoding.";
    return false;
  }

  pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

  hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr,
                                   pcmType.Get());
  if (FAILED(hr)) {
    outError = "Failed to configure MP3 decoder output as PCM: " + path.string();
    return false;
  }

  ComPtr<IMFMediaType> actualType;
  hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType);
  if (FAILED(hr) || !actualType) {
    outError = "Failed to query MP3 decoder output format: " + path.string();
    return false;
  }

  UINT32 channels = 0;
  UINT32 sampleRate = 0;
  UINT32 bitsPerSample = 0;
  actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
  actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
  actualType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);

  const ALenum alFormat =
      alFormatFor(static_cast<std::uint16_t>(channels),
                  static_cast<std::uint16_t>(bitsPerSample));
  if (alFormat == 0) {
    outError = "Unsupported MP3 channel/bit-depth combination: " + path.string();
    return false;
  }

  std::vector<char> pcmBytes;
  while (true) {
    DWORD streamFlags = 0;
    ComPtr<IMFSample> sample;
    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr,
                            &streamFlags, nullptr, &sample);
    if (FAILED(hr)) {
      outError = "Failed while decoding MP3 file: " + path.string();
      return false;
    }

    if ((streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
      break;
    }

    if (!sample) {
      continue;
    }

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr) || !buffer) {
      outError = "Failed to access MP3 decoder buffer: " + path.string();
      return false;
    }

    BYTE* data = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = buffer->Lock(&data, &maxLength, &currentLength);
    if (FAILED(hr)) {
      outError = "Failed to lock MP3 decoder buffer: " + path.string();
      return false;
    }

    const std::size_t offset = pcmBytes.size();
    pcmBytes.resize(offset + currentLength);
    if (currentLength > 0) {
      std::memcpy(pcmBytes.data() + offset, data, currentLength);
    }
    buffer->Unlock();
  }

  outData.format.alFormat = alFormat;
  outData.format.channels = static_cast<std::uint16_t>(channels);
  outData.format.bitsPerSample = static_cast<std::uint16_t>(bitsPerSample);
  outData.format.sampleRate = static_cast<std::uint32_t>(sampleRate);
  outData.format.bytesPerFrame =
      static_cast<std::uint32_t>(channels) *
      static_cast<std::uint32_t>(bitsPerSample / 8u);
  outData.pcmData = std::move(pcmBytes);
  outData.durationSeconds =
      computeDurationSeconds(outData.format, outData.pcmData.size());
  return true;
}

} // namespace

OpenAlContext::~OpenAlContext() {
  shutdown();
}

bool OpenAlContext::initialize(std::string& outError) {
  shutdown();

  device_ = alcOpenDevice(nullptr);
  if (!device_) {
    outError = "Failed to open the default OpenAL device.";
    return false;
  }

  context_ = alcCreateContext(device_, nullptr);
  if (!context_) {
    outError = "Failed to create the OpenAL context.";
    shutdown();
    return false;
  }

  if (!alcMakeContextCurrent(context_)) {
    outError = "Failed to activate the OpenAL context.";
    shutdown();
    return false;
  }

  alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
  alDopplerFactor(1.0f);
  alSpeedOfSound(343.3f);
  outError.clear();
  return true;
}

void OpenAlContext::shutdown() {
  if (context_) {
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context_);
    context_ = nullptr;
  }

  if (device_) {
    alcCloseDevice(device_);
    device_ = nullptr;
  }
}

bool loadAudioFile(const std::filesystem::path& path, DecodedAudioData& outData,
                   std::string& outError) {
  outData = {};
  outError.clear();

  try {
    const std::string extension = lowerExtension(path);
    if (extension == ".wav") {
      return decodeWavFile(path, outData, outError);
    }
    if (extension == ".ogg") {
      return decodeOggFile(path, outData, outError);
    }
    if (extension == ".mp3") {
      return decodeMp3File(path, outData, outError);
    }

    outError = "Unsupported audio format '" + extension +
               "' for file: " + path.string();
    return false;
  } catch (const std::bad_alloc&) {
    outData = {};
    outError = "Audio decode ran out of memory for file: " + path.string();
    return false;
  }
}

bool AudioStreamReader::open(const std::filesystem::path& path,
                             std::string& outError) {
  close();

  if (lowerExtension(path) == ".wav") {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      outError = "Failed to open WAV file: " + path.string();
      return false;
    }

    WavDecodeInfo info{};
    if (!parseWavHeader(file, info, outError)) {
      outError += " (" + path.string() + ")";
      return false;
    }

    format_ = info.format;
    durationSeconds_ = computeDurationSeconds(info.format, info.dataSize);
    streamDataOffset_ = info.dataOffset;
    streamDataSize_ = info.dataSize;
    readOffset_ = 0u;
    usesFileStreaming_ = true;

    file.seekg(static_cast<std::streamoff>(streamDataOffset_), std::ios::beg);
    if (!file.good()) {
      outError = "Failed to seek WAV stream data: " + path.string();
      close();
      return false;
    }

    streamedFile_ = std::move(file);
    outError.clear();
    return true;
  }

  auto decodedData = std::make_shared<DecodedAudioData>();
  if (!loadAudioFile(path, *decodedData, outError)) {
    return false;
  }

  decodedData_ = std::move(decodedData);
  format_ = decodedData_->format;
  durationSeconds_ = decodedData_->durationSeconds;
  readOffset_ = 0u;
  return true;
}

bool AudioStreamReader::open(
    const std::shared_ptr<const DecodedAudioData>& decodedData) {
  close();

  if (!decodedData || decodedData->format.alFormat == 0 ||
      decodedData->pcmData.empty()) {
    return false;
  }

  decodedData_ = decodedData;
  format_ = decodedData_->format;
  durationSeconds_ = decodedData_->durationSeconds;
  readOffset_ = 0u;
  usesFileStreaming_ = false;
  return true;
}

void AudioStreamReader::close() {
  decodedData_.reset();
  if (streamedFile_.is_open()) {
    streamedFile_.close();
  }
  format_ = {};
  durationSeconds_ = 0.0;
  readOffset_ = 0u;
  streamDataOffset_ = 0u;
  streamDataSize_ = 0u;
  usesFileStreaming_ = false;
}

std::size_t AudioStreamReader::read(char* destination, std::size_t maxBytes) {
  if (!valid() || !destination || maxBytes == 0u || finished()) {
    return 0u;
  }

  if (usesFileStreaming_) {
    std::size_t bytesToRead = maxBytes;
    if (format_.bytesPerFrame > 0u) {
      bytesToRead -= bytesToRead % format_.bytesPerFrame;
    }
    if (bytesToRead == 0u) {
      bytesToRead = maxBytes;
    }

    const std::size_t remaining =
        static_cast<std::size_t>(streamDataSize_ - readOffset_);
    bytesToRead = std::min(bytesToRead, remaining);
    if (bytesToRead == 0u) {
      return 0u;
    }

    streamedFile_.read(destination, static_cast<std::streamsize>(bytesToRead));
    const std::size_t bytesRead = static_cast<std::size_t>(streamedFile_.gcount());
    readOffset_ += bytesRead;
    return bytesRead;
  }

  const std::vector<char>& pcmData = decodedData_->pcmData;

  std::size_t bytesToRead = maxBytes;
  if (format_.bytesPerFrame > 0u) {
    bytesToRead -= bytesToRead % format_.bytesPerFrame;
  }
  if (bytesToRead == 0u) {
    bytesToRead = maxBytes;
  }

  bytesToRead = std::min(bytesToRead, pcmData.size() - readOffset_);
  if (bytesToRead == 0u) {
    return 0u;
  }

  std::memcpy(destination, pcmData.data() + readOffset_, bytesToRead);
  readOffset_ += bytesToRead;
  return bytesToRead;
}

void AudioStreamReader::rewind() {
  readOffset_ = 0u;
  if (usesFileStreaming_ && streamedFile_.is_open()) {
    streamedFile_.clear();
    streamedFile_.seekg(static_cast<std::streamoff>(streamDataOffset_), std::ios::beg);
  }
}

} // namespace AUDIO
} // namespace ENGINE
