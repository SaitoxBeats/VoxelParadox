#include "audio/audio_manager.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "audio/audio_data.hpp"
#include "audio/openal_backend.hpp"
#include "path/app_paths.hpp"

namespace ENGINE {
namespace AUDIO {
namespace {

constexpr std::size_t kPreferredSfxSourceCount = 48u;
constexpr std::size_t kMusicStreamCount = 2u;
constexpr std::size_t kMusicBufferCount = 3u;
constexpr std::size_t kMusicChunkBytes = 65536u;

void logAudioMessage(const char* level, const std::string& message) {
  std::printf("[Audio][%s] %s\n", level, message.c_str());
}

std::uint32_t makeSourceHandleValue(std::size_t index, std::uint16_t generation) {
  return (static_cast<std::uint32_t>(generation) << 16u) |
         static_cast<std::uint32_t>(index + 1u);
}

std::size_t sourceIndexFromHandle(AudioSourceHandle handle) {
  return handle.valid() ? static_cast<std::size_t>((handle.value & 0xFFFFu) - 1u)
                        : std::numeric_limits<std::size_t>::max();
}

std::uint16_t sourceGenerationFromHandle(AudioSourceHandle handle) {
  return static_cast<std::uint16_t>(handle.value >> 16u);
}

float sanitizePositive(float value, float fallback = 1.0f) {
  if (!(value == value) || value <= 0.0f) {
    return fallback;
  }
  return value;
}

bool isWavMusicTrackPath(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".wav";
}

struct EventRuntimeState {
  std::deque<std::size_t> recentIndices;
  std::vector<std::size_t> shuffleBag;
};

struct LoadedBuffer {
  AudioBufferHandle handle{};
  ALuint alBuffer = 0;
  PcmFormatInfo format{};
  double durationSeconds = 0.0;
  std::string pathKey;
};

struct SourceSlot {
  ALuint source = 0;
  bool active = false;
  bool looping = false;
  bool pausedByGame = false;
  SoundEventId eventId{};
  SoundCategoryId category = SoundCategoryId::Block;
  AudioBufferHandle bufferHandle{};
  int priority = 0;
  float baseGain = 1.0f;
  float requestGain = 1.0f;
  float variationGain = 1.0f;
  float fadeGain = 1.0f;
  float fadeStart = 1.0f;
  float fadeTarget = 1.0f;
  float fadeDuration = 0.0f;
  float fadeElapsed = 0.0f;
  bool positional = false;
  float referenceDistance = 3.0f;
  float maxDistance = 48.0f;
  float rolloffFactor = 1.0f;
  glm::vec3 position{0.0f};
  glm::vec3 velocity{0.0f};
  std::uint16_t generation = 0;
  AudioSourceHandle handle{};
};

struct MusicRuntimeState {
  ALuint source = 0;
  std::array<ALuint, kMusicBufferCount> buffers{};
  bool ready = false;
  bool active = false;
  bool finishedReading = false;
  bool stopAfterFade = false;
  float fadeGain = 0.0f;
  float fadeStart = 0.0f;
  float fadeTarget = 1.0f;
  float fadeDuration = 0.0f;
  float fadeElapsed = 0.0f;
  double trackDurationSeconds = 0.0;
  double playbackSeconds = 0.0;
  std::string contextName;
  std::string trackId;
  std::string trackPathKey;
  std::vector<char> scratch;
  AudioStreamReader reader;
};

struct MusicDecodeState {
  enum class Status : std::uint8_t {
    Loading = 0,
    Ready,
    Failed,
  };

  mutable std::mutex mutex;
  Status status = Status::Loading;
  std::shared_ptr<DecodedAudioData> decodedData;
  std::string error;
};

struct MusicPreloadJob {
  std::filesystem::path path;
  std::string pathKey;
  std::shared_ptr<MusicDecodeState> state;
};

} // namespace

struct AudioManager::Impl {
  OpenAlContext backend;
  AudioDatabase database;
  AudioSettings settings;
  AudioListenerState listenerState;
  std::mt19937 rng{std::random_device{}()};
  std::vector<SourceSlot> sources;
  std::vector<EventRuntimeState> eventStates;
  std::unordered_map<std::string, AudioBufferHandle> bufferLookup;
  std::vector<LoadedBuffer> buffers;
  std::array<MusicRuntimeState, kMusicStreamCount> musicStreams;
  std::vector<std::string> activeMusicTags;
  std::unordered_map<std::string, std::size_t> nextMusicTrackIndices;
  std::unordered_map<std::string, std::shared_ptr<MusicDecodeState>> musicDecodeCache;
  std::mutex musicDecodeCacheMutex;
  std::deque<MusicPreloadJob> musicPreloadQueue;
  std::mutex musicPreloadQueueMutex;
  std::condition_variable musicPreloadQueueCv;
  std::thread musicPreloadWorker;
  bool musicPreloadWorkerStopRequested = false;
  const MusicContextDefinition* desiredMusicContext = nullptr;
  int primaryMusicStreamIndex = -1;
  bool initialized = false;
  bool backendReady = false;
  bool silentMode = true;
  bool musicImmediateRequest = false;
  bool gameplayPaused = false;

  ~Impl() { shutdown(); }

  void startMusicPreloadWorker() {
    stopMusicPreloadWorker();

    musicPreloadWorkerStopRequested = false;
    musicPreloadWorker = std::thread([this] {
      try {
        runMusicPreloadWorker();
      } catch (const std::exception& exception) {
        std::fprintf(stderr,
                     "[Audio][WARN] Music preload worker terminated with an exception: %s\n",
                     exception.what());
        std::fflush(stderr);
      } catch (...) {
        std::fprintf(stderr,
                     "[Audio][WARN] Music preload worker terminated with an unknown exception.\n");
        std::fflush(stderr);
      }
    });
  }

  void stopMusicPreloadWorker() {
    {
      std::scoped_lock<std::mutex> lock(musicPreloadQueueMutex);
      musicPreloadWorkerStopRequested = true;
    }
    musicPreloadQueueCv.notify_all();

    if (musicPreloadWorker.joinable()) {
      musicPreloadWorker.join();
    }

    std::scoped_lock<std::mutex> lock(musicPreloadQueueMutex);
    musicPreloadQueue.clear();
    musicPreloadWorkerStopRequested = false;
  }

  void runMusicPreloadWorker() {
    while (true) {
      MusicPreloadJob job;
      {
        std::unique_lock<std::mutex> lock(musicPreloadQueueMutex);
        musicPreloadQueueCv.wait(lock, [this] {
          return musicPreloadWorkerStopRequested || !musicPreloadQueue.empty();
        });

        if (musicPreloadWorkerStopRequested && musicPreloadQueue.empty()) {
          return;
        }

        job = std::move(musicPreloadQueue.front());
        musicPreloadQueue.pop_front();
      }

      try {
        auto decodedData = std::make_shared<DecodedAudioData>();
        std::string error;
        const bool loaded = loadAudioFile(job.path, *decodedData, error);

        std::scoped_lock<std::mutex> stateLock(job.state->mutex);
        if (loaded) {
          job.state->status = MusicDecodeState::Status::Ready;
          job.state->decodedData = std::move(decodedData);
          job.state->error.clear();
          continue;
        }

        job.state->status = MusicDecodeState::Status::Failed;
        job.state->decodedData.reset();
        job.state->error = std::move(error);
      } catch (const std::exception& exception) {
        {
          std::scoped_lock<std::mutex> stateLock(job.state->mutex);
          job.state->status = MusicDecodeState::Status::Failed;
          job.state->decodedData.reset();
        }

        try {
          const std::string error =
              "Music preload threw an exception for '" + job.path.string() + "': " +
              exception.what();
          {
            std::scoped_lock<std::mutex> stateLock(job.state->mutex);
            job.state->error = error;
          }
          logAudioMessage("WARN", error);
        } catch (...) {
          std::fprintf(stderr,
                       "[Audio][WARN] Music preload failed after an exception, and the error message could not be materialized.\n");
          std::fflush(stderr);
        }
      } catch (...) {
        {
          std::scoped_lock<std::mutex> stateLock(job.state->mutex);
          job.state->status = MusicDecodeState::Status::Failed;
          job.state->decodedData.reset();
        }

        try {
          const std::string error =
              "Music preload threw an unknown exception for '" + job.path.string() + "'.";
          {
            std::scoped_lock<std::mutex> stateLock(job.state->mutex);
            job.state->error = error;
          }
          logAudioMessage("WARN", error);
        } catch (...) {
          std::fprintf(stderr,
                       "[Audio][WARN] Music preload failed with an unknown exception, and the error message could not be materialized.\n");
          std::fflush(stderr);
        }
      }
    }
  }

  void shutdown() {
    stopMusicPreloadWorker();
    stopAllMusicTracks();

    for (SourceSlot& slot : sources) {
      if (slot.source != 0) {
        alSourceStop(slot.source);
      }
    }
    for (SourceSlot& slot : sources) {
      if (slot.source != 0) {
        alDeleteSources(1, &slot.source);
      }
    }
    sources.clear();

    for (LoadedBuffer& buffer : buffers) {
      if (buffer.alBuffer != 0) {
        alDeleteBuffers(1, &buffer.alBuffer);
      }
    }
    buffers.clear();
    bufferLookup.clear();
    eventStates.clear();

    for (MusicRuntimeState& music : musicStreams) {
      if (music.source != 0) {
        alDeleteSources(1, &music.source);
        music.source = 0;
      }
      alDeleteBuffers(static_cast<ALsizei>(music.buffers.size()),
                      music.buffers.data());
      music.buffers.fill(0);
      music.ready = false;
      music.scratch.clear();
    }

    backend.shutdown();
    backendReady = false;
    silentMode = true;
    initialized = false;
    desiredMusicContext = nullptr;
    primaryMusicStreamIndex = -1;
    activeMusicTags.clear();
    nextMusicTrackIndices.clear();
    gameplayPaused = false;
    std::scoped_lock<std::mutex> lock(musicDecodeCacheMutex);
    musicDecodeCache.clear();
  }

  float categoryGain(SoundCategoryId category) const {
    const float masterVolume = settings.volumeFor(SoundCategoryId::Master);
    if (settings.mutedFor(SoundCategoryId::Master) || masterVolume <= 0.0f) {
      return 0.0f;
    }

    if (category == SoundCategoryId::Master) {
      return masterVolume;
    }

    if (settings.mutedFor(category)) {
      return 0.0f;
    }

    return masterVolume * settings.volumeFor(category);
  }

  void applyListener() {
    if (!backendReady) {
      return;
    }

    const float orientation[6] = {
        listenerState.forward.x, listenerState.forward.y, listenerState.forward.z,
        listenerState.up.x,      listenerState.up.y,      listenerState.up.z,
    };
    alListener3f(AL_POSITION, listenerState.position.x, listenerState.position.y,
                 listenerState.position.z);
    alListener3f(AL_VELOCITY, listenerState.velocity.x, listenerState.velocity.y,
                 listenerState.velocity.z);
    alListenerfv(AL_ORIENTATION, orientation);
  }

  void stopSourceSlot(SourceSlot& slot) {
    if (slot.source != 0) {
      alSourceStop(slot.source);
      alSourcei(slot.source, AL_BUFFER, 0);
    }
    slot.active = false;
    slot.looping = false;
    slot.pausedByGame = false;
    slot.eventId = {};
    slot.bufferHandle = {};
    slot.priority = 0;
    slot.baseGain = 1.0f;
    slot.requestGain = 1.0f;
    slot.variationGain = 1.0f;
    slot.fadeGain = 1.0f;
    slot.fadeStart = 1.0f;
    slot.fadeTarget = 1.0f;
    slot.fadeDuration = 0.0f;
    slot.fadeElapsed = 0.0f;
    slot.positional = false;
    slot.referenceDistance = 3.0f;
    slot.maxDistance = 48.0f;
    slot.rolloffFactor = 1.0f;
    slot.position = glm::vec3(0.0f);
    slot.velocity = glm::vec3(0.0f);
    slot.handle = {};
  }

  void stopAllActiveSources() {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      if (slot.active) {
        stopSourceSlot(slot);
      }
    }
  }

  void reclaimFinishedSources() {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      if (!slot.active || slot.looping) {
        continue;
      }

      ALint state = AL_STOPPED;
      alGetSourcei(slot.source, AL_SOURCE_STATE, &state);
      if (state == AL_STOPPED) {
        stopSourceSlot(slot);
      }
    }
  }

  void pauseGameplaySources() {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      slot.pausedByGame = false;
      if (!slot.active || slot.source == 0) {
        continue;
      }

      ALint state = AL_STOPPED;
      alGetSourcei(slot.source, AL_SOURCE_STATE, &state);
      if (state != AL_PLAYING) {
        continue;
      }

      alSourcePause(slot.source);
      slot.pausedByGame = (alGetError() == AL_NO_ERROR);
    }
  }

  void resumeGameplaySources() {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      if (!slot.active || !slot.pausedByGame || slot.source == 0) {
        continue;
      }

      alSourcePlay(slot.source);
      slot.pausedByGame = false;
    }
  }

  void setGameplayPaused(bool paused) {
    if (gameplayPaused == paused) {
      return;
    }

    gameplayPaused = paused;
    if (gameplayPaused) {
      pauseGameplaySources();
      return;
    }

    resumeGameplaySources();
  }

  SourceSlot* acquireSourceSlot(int priority, bool looping) {
    reclaimFinishedSources();

    for (SourceSlot& slot : sources) {
      if (!slot.active) {
        return &slot;
      }
    }

    SourceSlot* victim = nullptr;
    for (SourceSlot& slot : sources) {
      if (!victim) {
        victim = &slot;
        continue;
      }

      if (slot.priority < victim->priority ||
          (slot.priority == victim->priority && !slot.looping && victim->looping)) {
        victim = &slot;
      }
    }

    if (!victim || victim->priority > priority) {
      return nullptr;
    }

    stopSourceSlot(*victim);
    return victim;
  }

  LoadedBuffer* findBuffer(AudioBufferHandle handle) {
    if (!handle.valid()) {
      return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(handle.value - 1u);
    return index < buffers.size() ? &buffers[index] : nullptr;
  }

  LoadedBuffer* ensureBufferLoaded(const AudioClipDefinition& clip) {
    const std::string key = clip.path.lexically_normal().generic_string();
    const auto it = bufferLookup.find(key);
    if (it != bufferLookup.end()) {
      return findBuffer(it->second);
    }

    DecodedAudioData data;
    std::string error;
    if (!loadAudioFile(clip.path, data, error)) {
      logAudioMessage("WARN", error);
      return nullptr;
    }

    LoadedBuffer buffer;
    buffer.handle.value = static_cast<std::uint32_t>(buffers.size() + 1u);
    buffer.pathKey = key;
    buffer.format = data.format;
    buffer.durationSeconds = data.durationSeconds;

    alGenBuffers(1, &buffer.alBuffer);
    if (buffer.alBuffer == 0) {
      logAudioMessage("WARN", "Failed to allocate an OpenAL buffer.");
      return nullptr;
    }

    alBufferData(buffer.alBuffer, data.format.alFormat, data.pcmData.data(),
                 static_cast<ALsizei>(data.pcmData.size()),
                 static_cast<ALsizei>(data.format.sampleRate));
    if (alGetError() != AL_NO_ERROR) {
      alDeleteBuffers(1, &buffer.alBuffer);
      logAudioMessage("WARN", "OpenAL rejected a decoded audio buffer.");
      return nullptr;
    }

    buffers.push_back(buffer);
    bufferLookup[key] = buffer.handle;
    return &buffers.back();
  }

  std::size_t eventActiveCount(SoundEventId id) const {
    std::size_t count = 0u;
    for (const SourceSlot& slot : sources) {
      if (slot.active && slot.eventId.value == id.value) {
        count++;
      }
    }
    return count;
  }

  float sampleRange(const FloatRange& range) {
    if (range.max <= range.min) {
      return range.min;
    }
    std::uniform_real_distribution<float> distribution(range.min, range.max);
    return distribution(rng);
  }

  std::size_t chooseClipIndex(const SoundEventDefinition& event) {
    if (event.clips.size() <= 1u) {
      return 0u;
    }

    EventRuntimeState& state =
        eventStates[static_cast<std::size_t>(event.id.value - 1u)];
    std::uniform_int_distribution<std::size_t> distribution(0u,
                                                            event.clips.size() - 1u);

    if (event.antiRepeat == AntiRepeatPolicy::ShuffleBag &&
        event.clips.size() >= 3u) {
      if (state.shuffleBag.empty()) {
        state.shuffleBag.resize(event.clips.size());
        for (std::size_t index = 0u; index < event.clips.size(); index++) {
          state.shuffleBag[index] = index;
        }
        std::shuffle(state.shuffleBag.begin(), state.shuffleBag.end(), rng);
        if (!state.recentIndices.empty() &&
            state.shuffleBag.front() == state.recentIndices.back() &&
            state.shuffleBag.size() > 1u) {
          std::swap(state.shuffleBag.front(), state.shuffleBag.back());
        }
      }

      const std::size_t chosen = state.shuffleBag.back();
      state.shuffleBag.pop_back();
      state.recentIndices.push_back(chosen);
      while (state.recentIndices.size() > 2u) {
        state.recentIndices.pop_front();
      }
      return chosen;
    }

    std::size_t chosen = distribution(rng);
    if (!state.recentIndices.empty()) {
      for (int attempt = 0; attempt < 4; attempt++) {
        if (chosen != state.recentIndices.back()) {
          break;
        }
        chosen = distribution(rng);
      }
    }

    state.recentIndices.push_back(chosen);
    const std::size_t maxHistory = event.clips.size() > 2u ? 2u : 1u;
    while (state.recentIndices.size() > maxHistory) {
      state.recentIndices.pop_front();
    }
    return chosen;
  }

  void applySourceGain(SourceSlot& slot) {
    const float categoryVolume = categoryGain(slot.category);
    const float finalGain =
        categoryVolume * slot.baseGain * slot.requestGain * slot.variationGain *
        slot.fadeGain;
    alSourcef(slot.source, AL_GAIN, std::max(0.0f, finalGain));
  }

  void refreshAllSourceGains() {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      if (slot.active) {
        applySourceGain(slot);
      }
    }
    for (MusicRuntimeState& music : musicStreams) {
      applyMusicGain(music);
    }
  }

  void updateSourceFades(float dtSeconds) {
    if (!backendReady) {
      return;
    }

    for (SourceSlot& slot : sources) {
      if (!slot.active || slot.fadeDuration <= 0.0f) {
        continue;
      }

      slot.fadeElapsed = std::min(slot.fadeElapsed + dtSeconds, slot.fadeDuration);
      const float t =
          slot.fadeDuration > 0.0f ? (slot.fadeElapsed / slot.fadeDuration) : 1.0f;
      slot.fadeGain = slot.fadeStart + (slot.fadeTarget - slot.fadeStart) * t;
      if (slot.fadeElapsed < slot.fadeDuration) {
        continue;
      }

      slot.fadeGain = slot.fadeTarget;
      slot.fadeStart = slot.fadeTarget;
      slot.fadeDuration = 0.0f;
      slot.fadeElapsed = 0.0f;
    }
  }

  void applyMusicGain(MusicRuntimeState& music) {
    if (!backendReady || !music.ready || music.source == 0) {
      return;
    }

    const float finalGain = categoryGain(SoundCategoryId::Music) * music.fadeGain;
    alSourcef(music.source, AL_GAIN, std::max(0.0f, finalGain));
  }

  bool createSources() {
    if (!backendReady) {
      return false;
    }

    sources.clear();
    sources.reserve(kPreferredSfxSourceCount);
    for (std::size_t index = 0u; index < kPreferredSfxSourceCount; index++) {
      SourceSlot slot;
      alGenSources(1, &slot.source);
      if (slot.source == 0 || alGetError() != AL_NO_ERROR) {
        break;
      }
      sources.push_back(slot);
    }

    if (sources.empty()) {
      logAudioMessage("WARN", "No OpenAL SFX sources could be created.");
      return false;
    }

    bool anyMusicReady = false;
    for (MusicRuntimeState& music : musicStreams) {
      music.scratch.resize(kMusicChunkBytes);
      alGenSources(1, &music.source);
      alGenBuffers(static_cast<ALsizei>(music.buffers.size()), music.buffers.data());
      music.ready = music.source != 0 && alGetError() == AL_NO_ERROR;
      anyMusicReady = anyMusicReady || music.ready;
      if (music.ready) {
        continue;
      }

      logAudioMessage("WARN", "Music streaming source creation failed.");
      if (music.source != 0) {
        alDeleteSources(1, &music.source);
        music.source = 0;
      }
      alDeleteBuffers(static_cast<ALsizei>(music.buffers.size()),
                      music.buffers.data());
      music.buffers.fill(0);
    }

    return anyMusicReady;
  }

  void initializeEventRuntime() {
    eventStates.clear();
    eventStates.resize(database.events().size());
  }

  AudioSourceHandle playEventById(SoundEventId id,
                                  const SoundPlaybackRequest& request) {
    if (!backendReady || gameplayPaused) {
      return {};
    }

    const SoundEventDefinition* event = database.findEvent(id);
    if (!event || event->clips.empty()) {
      return {};
    }
    if (eventActiveCount(id) >= static_cast<std::size_t>(event->maxConcurrency)) {
      return {};
    }

    const std::size_t clipIndex = chooseClipIndex(*event);
    LoadedBuffer* buffer = ensureBufferLoaded(event->clips[clipIndex]);
    if (!buffer) {
      return {};
    }

    const bool looping = request.hasLoopOverride ? request.looping : event->loop;
    SourceSlot* slot = acquireSourceSlot(event->priority + request.priorityBias, looping);
    if (!slot) {
      return {};
    }

    stopSourceSlot(*slot);
    slot->generation = static_cast<std::uint16_t>(slot->generation + 1u);
    if (slot->generation == 0u) {
      slot->generation = 1u;
    }
    slot->handle.value =
        makeSourceHandleValue(static_cast<std::size_t>(slot - sources.data()),
                              slot->generation);
    slot->active = true;
    slot->looping = looping;
    slot->eventId = event->id;
    slot->bufferHandle = buffer->handle;
    slot->category = event->category;
    slot->priority = event->priority + request.priorityBias;
    slot->baseGain = std::max(0.0f, event->baseGain);
    slot->requestGain = std::max(0.0f, request.gain);
    slot->variationGain = std::max(0.0f, sampleRange(event->volumeRange));
    const float fadeInSeconds =
        request.hasFadeInOverride ? std::max(0.0f, request.fadeInSeconds)
                                  : event->fadeInSeconds;
    slot->fadeGain = fadeInSeconds > 0.0f ? 0.0f : 1.0f;
    slot->fadeStart = slot->fadeGain;
    slot->fadeTarget = 1.0f;
    slot->fadeDuration = fadeInSeconds;
    slot->fadeElapsed = 0.0f;
    slot->positional = request.hasPosition && (request.positional || event->positional);
    slot->referenceDistance = event->referenceDistance;
    slot->maxDistance = event->maxDistance;
    slot->rolloffFactor = event->rolloffFactor;
    slot->position = request.position;
    slot->velocity = request.velocity;

    const float pitch = sanitizePositive(request.pitch) *
                        std::max(0.01f, sampleRange(event->pitchRange));

    alSourceStop(slot->source);
    alSourcei(slot->source, AL_BUFFER, 0);
    alSourcei(slot->source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    alSourcef(slot->source, AL_PITCH, pitch);
    alSourcei(slot->source, AL_BUFFER, static_cast<ALint>(buffer->alBuffer));

    if (slot->positional) {
      alSourcei(slot->source, AL_SOURCE_RELATIVE, AL_FALSE);
      alSource3f(slot->source, AL_POSITION, request.position.x, request.position.y,
                 request.position.z);
      alSource3f(slot->source, AL_VELOCITY, request.velocity.x, request.velocity.y,
                 request.velocity.z);
      alSourcef(slot->source, AL_REFERENCE_DISTANCE, event->referenceDistance);
      alSourcef(slot->source, AL_MAX_DISTANCE, event->maxDistance);
      alSourcef(slot->source, AL_ROLLOFF_FACTOR, event->rolloffFactor);
    } else {
      alSourcei(slot->source, AL_SOURCE_RELATIVE, AL_TRUE);
      alSource3f(slot->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
      alSource3f(slot->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
      alSourcef(slot->source, AL_REFERENCE_DISTANCE, 1.0f);
      alSourcef(slot->source, AL_MAX_DISTANCE, 1.0f);
      alSourcef(slot->source, AL_ROLLOFF_FACTOR, 0.0f);
    }

    applySourceGain(*slot);
    alSourcePlay(slot->source);
    if (alGetError() != AL_NO_ERROR) {
      stopSourceSlot(*slot);
      return {};
    }

    return slot->handle;
  }

  std::string musicPathKey(const std::filesystem::path& path) const {
    return path.lexically_normal().generic_string();
  }

  void requestMusicPreload(const MusicTrackDefinition* track) {
    if (!track) {
      return;
    }
    requestMusicPreload(track->path);
  }

  void requestContextMusicPreload(const MusicContextDefinition& context,
                                  std::size_t trackCount = 2u) {
    if (context.tracks.empty()) {
      return;
    }

    const std::size_t clampedCount = std::min(trackCount, context.tracks.size());
    for (std::size_t offset = 0u; offset < clampedCount; offset++) {
      requestMusicPreload(peekNextMusicTrack(context, offset));
    }
  }

  void trimMusicDecodeCache(
      const std::function<void(std::unordered_set<std::string>&)>& appendRetainedKeys = {}) {
    std::unordered_set<std::string> retainedKeys;
    retainedKeys.reserve(musicStreams.size() + 4u);

    for (const MusicRuntimeState& music : musicStreams) {
      if (!music.active || music.trackPathKey.empty()) {
        continue;
      }
      retainedKeys.insert(music.trackPathKey);
    }

    if (appendRetainedKeys) {
      appendRetainedKeys(retainedKeys);
    }

    std::scoped_lock<std::mutex> lock(musicDecodeCacheMutex);
    for (auto it = musicDecodeCache.begin(); it != musicDecodeCache.end();) {
      if (retainedKeys.find(it->first) != retainedKeys.end()) {
        ++it;
        continue;
      }

      MusicDecodeState::Status status = MusicDecodeState::Status::Loading;
      {
        std::scoped_lock<std::mutex> stateLock(it->second->mutex);
        status = it->second->status;
      }

      if (status == MusicDecodeState::Status::Loading) {
        ++it;
        continue;
      }

      it = musicDecodeCache.erase(it);
    }
  }

  const MusicTrackDefinition* peekNextMusicTrack(const MusicContextDefinition& context,
                                                 std::size_t offset = 0u) {
    if (context.tracks.empty()) {
      return nullptr;
    }

    const std::size_t nextIndex = nextMusicTrackIndices[context.name] % context.tracks.size();
    const std::size_t chosenIndex = (nextIndex + offset) % context.tracks.size();
    return &context.tracks[chosenIndex];
  }

  std::shared_ptr<MusicDecodeState> findMusicDecodeState(
      const std::string& pathKey) {
    std::scoped_lock<std::mutex> lock(musicDecodeCacheMutex);
    const auto found = musicDecodeCache.find(pathKey);
    return found != musicDecodeCache.end() ? found->second : nullptr;
  }

  void requestMusicPreload(const std::filesystem::path& path) {
    const std::string pathKey = musicPathKey(path);
    if (pathKey.empty() || isWavMusicTrackPath(path)) {
      return;
    }

    std::shared_ptr<MusicDecodeState> state;
    {
      std::scoped_lock<std::mutex> lock(musicDecodeCacheMutex);
      if (musicDecodeCache.find(pathKey) != musicDecodeCache.end()) {
        return;
      }

      state = std::make_shared<MusicDecodeState>();
      musicDecodeCache[pathKey] = state;
    }

    {
      std::scoped_lock<std::mutex> lock(musicPreloadQueueMutex);
      musicPreloadQueue.push_back(MusicPreloadJob{path, pathKey, std::move(state)});
    }
    musicPreloadQueueCv.notify_one();
  }

  const MusicTrackDefinition* consumeNextMusicTrack(const MusicContextDefinition& context) {
    const MusicTrackDefinition* track = peekNextMusicTrack(context);
    if (!track) {
      return nullptr;
    }

    requestMusicPreload(track);
    return track;
  }

  void adoptCurrentTrackForContext(const MusicContextDefinition& context,
                                   const std::string& trackId) {
    if (context.tracks.empty()) {
      nextMusicTrackIndices[context.name] = 0u;
      return;
    }

    std::size_t nextIndex = 0u;
    for (std::size_t index = 0u; index < context.tracks.size(); index++) {
      if (context.tracks[index].id == trackId) {
        nextIndex = (index + 1u) % context.tracks.size();
        break;
      }
    }
    nextMusicTrackIndices[context.name] = nextIndex;
  }

  bool tryGetPreloadedMusicTrack(
      const MusicTrackDefinition& track,
      std::shared_ptr<const DecodedAudioData>& outDecodedData, std::string& outError) {
    outDecodedData.reset();
    const std::shared_ptr<MusicDecodeState> state =
        findMusicDecodeState(musicPathKey(track.path));
    if (!state) {
      requestMusicPreload(track.path);
      return false;
    }

    std::shared_ptr<DecodedAudioData> decodedData;
    MusicDecodeState::Status status = MusicDecodeState::Status::Loading;
    {
      std::scoped_lock<std::mutex> stateLock(state->mutex);
      status = state->status;
      decodedData = state->decodedData;
      outError = state->error;
    }

    if (status == MusicDecodeState::Status::Ready && decodedData) {
      outError.clear();
      outDecodedData = std::static_pointer_cast<const DecodedAudioData>(decodedData);
      return true;
    }

    return false;
  }

  MusicRuntimeState* primaryMusicStream() {
    if (primaryMusicStreamIndex < 0 ||
        primaryMusicStreamIndex >= static_cast<int>(musicStreams.size())) {
      return nullptr;
    }
    MusicRuntimeState& music = musicStreams[static_cast<std::size_t>(primaryMusicStreamIndex)];
    return music.active ? &music : nullptr;
  }

  const MusicRuntimeState* primaryMusicStream() const {
    if (primaryMusicStreamIndex < 0 ||
        primaryMusicStreamIndex >= static_cast<int>(musicStreams.size())) {
      return nullptr;
    }
    const MusicRuntimeState& music =
        musicStreams[static_cast<std::size_t>(primaryMusicStreamIndex)];
    return music.active ? &music : nullptr;
  }

  MusicRuntimeState* findMusicStreamByTrack(const std::string& trackId) {
    for (MusicRuntimeState& music : musicStreams) {
      if (music.active && music.trackId == trackId) {
        return &music;
      }
    }
    return nullptr;
  }

  const MusicContextDefinition* contextDefinitionForName(
      const std::string& contextName) const {
    for (const MusicContextDefinition& context : database.musicContexts()) {
      if (context.name == contextName) {
        return &context;
      }
    }
    return nullptr;
  }

  const MusicTrackDefinition* findTrackInContext(const MusicContextDefinition& context,
                                                 const std::string& trackId) const {
    for (const MusicTrackDefinition& track : context.tracks) {
      if (track.id == trackId) {
        return &track;
      }
    }
    return nullptr;
  }

  int acquireMusicStreamIndex() {
    for (std::size_t index = 0u; index < musicStreams.size(); index++) {
      if (!musicStreams[index].active) {
        return static_cast<int>(index);
      }
    }

    for (std::size_t index = 0u; index < musicStreams.size(); index++) {
      if (static_cast<int>(index) == primaryMusicStreamIndex) {
        continue;
      }
      stopMusicTrack(musicStreams[index]);
      return static_cast<int>(index);
    }

    return primaryMusicStreamIndex >= 0 ? (1 - primaryMusicStreamIndex) : 0;
  }

  float musicTransitionSeconds(const MusicContextDefinition* fromContext,
                               const MusicContextDefinition& toContext) const {
    const float fadeInSeconds = std::max(0.0f, toContext.fadeInSeconds);
    const float fadeOutSeconds =
        std::max(0.0f, fromContext ? fromContext->fadeOutSeconds : fadeInSeconds);
    return std::max(fadeInSeconds, fadeOutSeconds);
  }

  void beginMusicFade(MusicRuntimeState& music, float target, float duration,
                      bool stopAfterFade) {
    music.fadeStart = music.fadeGain;
    music.fadeTarget = std::max(0.0f, target);
    music.fadeDuration = std::max(0.0f, duration);
    music.fadeElapsed = 0.0f;
    music.stopAfterFade = stopAfterFade;
    if (music.fadeDuration <= 0.0f) {
      music.fadeGain = music.fadeTarget;
      if (music.stopAfterFade && music.fadeTarget <= 0.0f) {
        stopMusicTrack(music);
      } else {
        applyMusicGain(music);
      }
    }
  }

  void stopMusicTrack(MusicRuntimeState& music) {
    if (!music.ready) {
      return;
    }

    alSourceStop(music.source);
    ALint queued = 0;
    alGetSourcei(music.source, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
      ALuint buffer = 0;
      alSourceUnqueueBuffers(music.source, 1, &buffer);
    }
    music.reader.close();
    music.active = false;
    music.finishedReading = false;
    music.stopAfterFade = false;
    music.fadeGain = 0.0f;
    music.fadeStart = 0.0f;
    music.fadeTarget = 1.0f;
    music.fadeDuration = 0.0f;
    music.fadeElapsed = 0.0f;
    music.trackDurationSeconds = 0.0;
    music.playbackSeconds = 0.0;
    music.contextName.clear();
    music.trackId.clear();
    music.trackPathKey.clear();
  }

  void stopAllMusicTracks() {
    for (MusicRuntimeState& music : musicStreams) {
      stopMusicTrack(music);
    }
    primaryMusicStreamIndex = -1;
    trimMusicDecodeCache();
  }

  bool queueMusicBuffer(MusicRuntimeState& music, ALuint bufferId) {
    if (!music.reader.valid() || !music.ready) {
      return false;
    }

    const std::size_t bytesRead =
        music.reader.read(music.scratch.data(), music.scratch.size());
    if (bytesRead == 0u) {
      music.finishedReading = true;
      return false;
    }

    const PcmFormatInfo& format = music.reader.format();
    alBufferData(bufferId, format.alFormat, music.scratch.data(),
                 static_cast<ALsizei>(bytesRead),
                 static_cast<ALsizei>(format.sampleRate));
    if (alGetError() != AL_NO_ERROR) {
      music.finishedReading = true;
      return false;
    }

    alSourceQueueBuffers(music.source, 1, &bufferId);
    return alGetError() == AL_NO_ERROR;
  }

  bool startMusicTrack(int streamIndex, const MusicContextDefinition& context,
                       const MusicTrackDefinition& track, float initialGain,
                       float targetGain, float fadeDuration) {
    if (streamIndex < 0 ||
        streamIndex >= static_cast<int>(musicStreams.size())) {
      return false;
    }

    MusicRuntimeState& music = musicStreams[static_cast<std::size_t>(streamIndex)];
    if (!music.ready || !settings.musicEnabled) {
      return false;
    }

    std::string error;
    stopMusicTrack(music);
    try {
      if (isWavMusicTrackPath(track.path)) {
        if (!music.reader.open(track.path, error)) {
          if (!error.empty()) {
            logAudioMessage("WARN", error);
          }
          return false;
        }
      } else {
        std::shared_ptr<const DecodedAudioData> decodedTrack;
        requestMusicPreload(&track);
        if (!tryGetPreloadedMusicTrack(track, decodedTrack, error)) {
          if (!error.empty()) {
            logAudioMessage("WARN", error);
          }
          return false;
        }

        if (!music.reader.open(decodedTrack)) {
          error = "Music preload finished, but the decoded track is invalid: " +
                  track.path.string();
          logAudioMessage("WARN", error);
          return false;
        }
      }
    } catch (const std::exception& exception) {
      logAudioMessage("WARN", "Failed to start music track '" + track.path.string() +
                                  "': " + exception.what());
      stopMusicTrack(music);
      return false;
    } catch (...) {
      logAudioMessage("WARN", "Failed to start music track '" + track.path.string() +
                                  "': unknown exception.");
      stopMusicTrack(music);
      return false;
    }

    music.active = true;
    music.finishedReading = false;
    music.contextName = context.name;
    music.trackId = track.id;
    music.trackPathKey = musicPathKey(track.path);
    music.trackDurationSeconds = music.reader.durationSeconds();
    music.playbackSeconds = 0.0;

    alSourcei(music.source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(music.source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(music.source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(music.source, AL_LOOPING, AL_FALSE);
    alSourcef(music.source, AL_PITCH, 1.0f);
    alSourcef(music.source, AL_ROLLOFF_FACTOR, 0.0f);

    bool queuedAnyBuffer = false;
    for (ALuint bufferId : music.buffers) {
      queuedAnyBuffer = queueMusicBuffer(music, bufferId) || queuedAnyBuffer;
    }

    if (!queuedAnyBuffer) {
      stopMusicTrack(music);
      return false;
    }

    music.fadeGain = glm::clamp(initialGain, 0.0f, 1.0f);
    applyMusicGain(music);
    if (fadeDuration > 0.0f && targetGain != music.fadeGain) {
      beginMusicFade(music, targetGain, fadeDuration, false);
    } else {
      music.fadeGain = glm::clamp(targetGain, 0.0f, 1.0f);
      applyMusicGain(music);
    }

    alSourcePlay(music.source);
    return true;
  }

  bool tryLoopCurrentTrack(MusicRuntimeState& music) {
    const MusicContextDefinition* context = contextDefinitionForName(music.contextName);
    if (!context) {
      return false;
    }

    const MusicTrackDefinition* nextTrack = peekNextMusicTrack(*context);
    if (!nextTrack || nextTrack->id != music.trackId) {
      return false;
    }

    adoptCurrentTrackForContext(*context, music.trackId);
    music.reader.rewind();
    music.finishedReading = false;
    music.playbackSeconds = 0.0;
    return true;
  }

  void updateMusicFade(MusicRuntimeState& music, float dtSeconds) {
    if (!music.active || music.fadeDuration <= 0.0f) {
      return;
    }

    music.fadeElapsed = std::min(music.fadeElapsed + dtSeconds, music.fadeDuration);
    const float t =
        music.fadeDuration > 0.0f ? (music.fadeElapsed / music.fadeDuration) : 1.0f;
    music.fadeGain = music.fadeStart + (music.fadeTarget - music.fadeStart) * t;
    applyMusicGain(music);
    if (music.fadeElapsed < music.fadeDuration) {
      return;
    }

    music.fadeDuration = 0.0f;
    music.fadeElapsed = 0.0f;
    if (music.stopAfterFade && music.fadeGain <= 0.0f) {
      stopMusicTrack(music);
      if (primaryMusicStream() == nullptr) {
        primaryMusicStreamIndex = -1;
      }
    }
  }

  void updateMusicStreaming(MusicRuntimeState& music, float dtSeconds) {
    if (!music.ready || !music.active) {
      return;
    }

    ALint state = AL_STOPPED;
    alGetSourcei(music.source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) {
      music.playbackSeconds =
          std::min(music.trackDurationSeconds, music.playbackSeconds + dtSeconds);
    }

    ALint processed = 0;
    alGetSourcei(music.source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
      ALuint bufferId = 0;
      alSourceUnqueueBuffers(music.source, 1, &bufferId);
      if (!music.finishedReading) {
        queueMusicBuffer(music, bufferId);
      } else if (tryLoopCurrentTrack(music)) {
        queueMusicBuffer(music, bufferId);
      }
    }

    alGetSourcei(music.source, AL_SOURCE_STATE, &state);
    ALint queued = 0;
    alGetSourcei(music.source, AL_BUFFERS_QUEUED, &queued);

    if (state != AL_PLAYING && queued > 0) {
      alSourcePlay(music.source);
    }

    if (queued == 0 && music.finishedReading) {
      stopMusicTrack(music);
      if (primaryMusicStream() == nullptr) {
        primaryMusicStreamIndex = -1;
      }
    }
  }

  void refreshDesiredMusicContext() {
    desiredMusicContext = database.resolveMusicContext(activeMusicTags);
  }

  bool currentTrackMatchesContext(const MusicContextDefinition& context) {
    MusicRuntimeState* music = primaryMusicStream();
    if (!music || !music->active) {
      return false;
    }

    const MusicTrackDefinition* matchingTrack = findTrackInContext(context, music->trackId);
    if (!matchingTrack) {
      return false;
    }

    music->contextName = context.name;
    adoptCurrentTrackForContext(context, music->trackId);
    return true;
  }

  bool transitionToTrack(const MusicContextDefinition& context,
                         const MusicTrackDefinition& track, bool immediateStart) {
    MusicRuntimeState* primary = primaryMusicStream();
    if (primary && (primary->trackId == track.id ||
                    primary->trackPathKey == musicPathKey(track.path))) {
      primary->contextName = context.name;
      adoptCurrentTrackForContext(context, primary->trackId);
      return true;
    }

    if (MusicRuntimeState* existing = findMusicStreamByTrack(track.id)) {
      primaryMusicStreamIndex =
          static_cast<int>(existing - musicStreams.data());
      existing->contextName = context.name;
      adoptCurrentTrackForContext(context, existing->trackId);
      if (primary && primary != existing) {
        const MusicContextDefinition* fromContext =
            contextDefinitionForName(primary->contextName);
        beginMusicFade(*primary, 0.0f, musicTransitionSeconds(fromContext, context), true);
      }
      return true;
    }

    const int newStreamIndex = acquireMusicStreamIndex();
    const MusicContextDefinition* fromContext =
        primary ? contextDefinitionForName(primary->contextName) : nullptr;
    const float transitionSeconds =
        immediateStart ? musicTransitionSeconds(fromContext, context) : 0.0f;
    const float startGain =
        primary ? 0.0f : (transitionSeconds > 0.0f ? 0.0f : 1.0f);
    const float targetGain = 1.0f;
    if (!startMusicTrack(newStreamIndex, context, track, startGain, targetGain,
                         transitionSeconds)) {
      return false;
    }
    adoptCurrentTrackForContext(context, track.id);

    if (primary && primary->active) {
      beginMusicFade(*primary, 0.0f, transitionSeconds, true);
    }

    primaryMusicStreamIndex = newStreamIndex;
    return true;
  }

  void requestUpcomingTrackPreload(const MusicContextDefinition& context) {
    requestContextMusicPreload(context, 2u);
  }

  void updateTrackEndTransition(const MusicContextDefinition& context) {
    MusicRuntimeState* primary = primaryMusicStream();
    if (!primary || !primary->active || primary->stopAfterFade) {
      return;
    }

    const float transitionSeconds = musicTransitionSeconds(&context, context);
    if (transitionSeconds <= 0.0f || primary->trackDurationSeconds <= 0.0) {
      return;
    }

    const double remainingSeconds =
        std::max(0.0, primary->trackDurationSeconds - primary->playbackSeconds);
    if (remainingSeconds > static_cast<double>(transitionSeconds)) {
      return;
    }

    const MusicTrackDefinition* nextTrack = peekNextMusicTrack(context);
    if (!nextTrack) {
      return;
    }

    if (nextTrack->id == primary->trackId) {
      return;
    }

    transitionToTrack(context, *consumeNextMusicTrack(context), true);
  }

  void updateMusic(float dtSeconds) {
    if (!backendReady) {
      return;
    }

    refreshDesiredMusicContext();
    for (MusicRuntimeState& music : musicStreams) {
      updateMusicFade(music, dtSeconds);
      updateMusicStreaming(music, dtSeconds);
    }

    if (!settings.musicEnabled || desiredMusicContext == nullptr) {
      for (MusicRuntimeState& music : musicStreams) {
        if (music.active && !music.stopAfterFade) {
          const MusicContextDefinition* context =
              contextDefinitionForName(music.contextName);
          beginMusicFade(music, 0.0f,
                         context ? std::max(0.0f, context->fadeOutSeconds) : 1.0f, true);
        }
      }
      musicImmediateRequest = false;
      return;
    }

    requestUpcomingTrackPreload(*desiredMusicContext);
    trimMusicDecodeCache([this](std::unordered_set<std::string>& retainedKeys) {
      if (desiredMusicContext == nullptr) {
        return;
      }

      const MusicTrackDefinition* nextTrack = peekNextMusicTrack(*desiredMusicContext, 0u);
      const MusicTrackDefinition* followingTrack = peekNextMusicTrack(*desiredMusicContext, 1u);
      if (nextTrack) {
        retainedKeys.insert(musicPathKey(nextTrack->path));
      }
      if (followingTrack) {
        retainedKeys.insert(musicPathKey(followingTrack->path));
      }
    });

    MusicRuntimeState* primary = primaryMusicStream();
    if (!primary) {
      const MusicTrackDefinition* firstTrack = consumeNextMusicTrack(*desiredMusicContext);
      if (firstTrack) {
        transitionToTrack(*desiredMusicContext, *firstTrack, true);
      }
      musicImmediateRequest = false;
      return;
    }

    const bool sameContext = primary->contextName == desiredMusicContext->name;
    if (!sameContext || musicImmediateRequest) {
      if (!currentTrackMatchesContext(*desiredMusicContext)) {
        const MusicTrackDefinition* desiredTrack = consumeNextMusicTrack(*desiredMusicContext);
        if (desiredTrack) {
          transitionToTrack(*desiredMusicContext, *desiredTrack, true);
        }
      }
    } else {
      updateTrackEndTransition(*desiredMusicContext);
    }

    musicImmediateRequest = false;
  }
};

AudioManager::AudioManager() : impl_(std::make_unique<Impl>()) {}

AudioManager::~AudioManager() = default;

bool AudioManager::initialize(const std::filesystem::path& manifestPath,
                              std::string* outMessage) {
  impl_->shutdown();

  std::vector<std::string> warnings;
  impl_->database.loadFromManifest(manifestPath, warnings);
  for (const std::string& warning : warnings) {
    logAudioMessage("WARN", warning);
  }
  impl_->initializeEventRuntime();
  impl_->settings.reset();
  impl_->settings.sanitize();

  std::string backendError;
  impl_->backendReady = impl_->backend.initialize(backendError);
  impl_->silentMode = !impl_->backendReady;
  impl_->initialized = true;

  if (!impl_->backendReady) {
    const std::string message =
        backendError.empty() ? std::string("Audio backend unavailable. Running silently.")
                             : backendError;
    logAudioMessage("WARN", message);
    if (outMessage) {
      *outMessage = message;
    }
    return true;
  }

  impl_->createSources();
  impl_->applyListener();
  impl_->refreshAllSourceGains();
  impl_->startMusicPreloadWorker();

  if (outMessage) {
    *outMessage = impl_->silentMode ? "Audio initialized in silent mode."
                                    : "Audio initialized.";
  }
  return true;
}

void AudioManager::shutdown() {
  impl_->shutdown();
}

bool AudioManager::backendReady() const {
  return impl_->backendReady;
}

bool AudioManager::silentMode() const {
  return impl_->silentMode;
}

void AudioManager::update(float dtSeconds) {
  if (!impl_->initialized || !impl_->backendReady) {
    return;
  }

  const float clampedDtSeconds = std::max(0.0f, dtSeconds);
  impl_->applyListener();
  impl_->refreshAllSourceGains();
  if (!impl_->gameplayPaused) {
    impl_->reclaimFinishedSources();
    impl_->updateSourceFades(clampedDtSeconds);
  }
  impl_->updateMusic(clampedDtSeconds);
}

void AudioManager::setGameplayPaused(bool paused) {
  impl_->setGameplayPaused(paused);
}

void AudioManager::setListenerState(const AudioListenerState& listenerState) {
  impl_->listenerState = listenerState;
  if (impl_->backendReady) {
    impl_->applyListener();
  }
}

void AudioManager::applySettings(const AudioSettings& settings) {
  impl_->settings = settings;
  impl_->settings.sanitize();
  if (impl_->backendReady) {
    impl_->refreshAllSourceGains();
  }
}

AudioSourceHandle AudioManager::playEvent(const std::string& eventName,
                                          const SoundPlaybackRequest& request) {
  const SoundEventDefinition* event = impl_->database.findEventByName(eventName);
  return event ? impl_->playEventById(event->id, request) : AudioSourceHandle{};
}

AudioSourceHandle AudioManager::playBlockAction(
    const std::string& blockId, BlockSoundAction action,
    const SoundPlaybackRequest& request) {
  const SoundEventId eventId = impl_->database.resolveBlockAction(blockId, action);
  return impl_->playEventById(eventId, request);
}

void AudioManager::stopAllActiveSounds() {
  impl_->stopAllActiveSources();
}

void AudioManager::setMusicRequest(const MusicPlaybackRequest& request) {
  impl_->activeMusicTags.clear();
  impl_->activeMusicTags.reserve(request.tags.size());
  for (const std::string& tag : request.tags) {
    const std::string normalized = normalizeAudioToken(tag);
    if (!normalized.empty()) {
      impl_->activeMusicTags.push_back(normalized);
    }
  }
  impl_->musicImmediateRequest = request.immediate || impl_->musicImmediateRequest;
}

void AudioManager::stopMusic(bool immediate) {
  if (!impl_->backendReady) {
    return;
  }

  if (immediate) {
    impl_->stopAllMusicTracks();
    return;
  }

  for (MusicRuntimeState& music : impl_->musicStreams) {
    if (!music.active) {
      continue;
    }
    const MusicContextDefinition* context =
        impl_->contextDefinitionForName(music.contextName);
    impl_->beginMusicFade(music, 0.0f,
                          context ? std::max(0.0f, context->fadeOutSeconds) : 1.0f,
                          true);
  }
}

std::string AudioManager::currentMusicContextName() const {
  if (const MusicRuntimeState* music = impl_->primaryMusicStream()) {
    return music->contextName;
  }
  return impl_->desiredMusicContext ? impl_->desiredMusicContext->name : std::string();
}

std::string AudioManager::currentMusicTrackId() const {
  if (const MusicRuntimeState* music = impl_->primaryMusicStream()) {
    return music->trackId;
  }
  return {};
}

std::size_t AudioManager::activeSourceCount() const {
  std::size_t count = 0u;
  for (const SourceSlot& slot : impl_->sources) {
    if (slot.active) {
      count++;
    }
  }
  return count;
}

std::size_t AudioManager::sourceCapacity() const {
  return impl_->sources.size();
}

} // namespace AUDIO
} // namespace ENGINE
