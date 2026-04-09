#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_types.hpp"

namespace ENGINE {
namespace AUDIO {

class AudioManager {
public:
  AudioManager();
  ~AudioManager();

  bool initialize(const std::filesystem::path& manifestPath = {},
                  std::string* outMessage = nullptr);
  void shutdown();

  bool backendReady() const;
  bool silentMode() const;

  void update(float dtSeconds);
  void setGameplayPaused(bool paused);
  void setListenerState(const AudioListenerState& listenerState);
  void applySettings(const AudioSettings& settings);

  AudioSourceHandle playEvent(const std::string& eventName,
                              const SoundPlaybackRequest& request = {});
  AudioSourceHandle playBlockAction(const std::string& blockId,
                                    BlockSoundAction action,
                                    const SoundPlaybackRequest& request = {});
  void stopAllActiveSounds();

  void setMusicRequest(const MusicPlaybackRequest& request);
  void stopMusic(bool immediate = false);

  std::string currentMusicContextName() const;
  std::string currentMusicTrackId() const;
  std::size_t activeSourceCount() const;
  std::size_t sourceCapacity() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace AUDIO
} // namespace ENGINE
