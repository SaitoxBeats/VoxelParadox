#include "runtime/runtime_ui_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "client_defaults.hpp"
#include "engine/bootstrap.hpp"
#include "input/input_action_system.hpp"

namespace RuntimeUI::Detail {

namespace {

void resetControlsMenuState(RuntimeUiState& uiState) {
  uiState.controlsCategoryIndex = 0;
  uiState.controlsCaptureOpen = false;
  uiState.controlsCaptureIgnoreMouseLeft = false;
  uiState.controlsCaptureActionId.clear();
  uiState.controlsWarningMessage.clear();
}

} // namespace

const char* renderDistanceDisplayName(WorldStack::RenderDistancePreset preset) {
  switch (preset) {
    case WorldStack::RenderDistancePreset::SHORT:
      return "Short";
    case WorldStack::RenderDistancePreset::NORMAL:
      return "Normal";
    case WorldStack::RenderDistancePreset::FAR:
      return "Far";
    case WorldStack::RenderDistancePreset::ULTRA:
      return "Ultra";
  }
  return "Normal";
}

std::string mouseSensitivityText(float sensitivity) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.4f", sensitivity);
  return buffer;
}

std::string renderScaleText(float renderScale) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f%%",
                glm::clamp(renderScale, ClientDefaults::kMinRenderScale,
                           ClientDefaults::kMaxRenderScale) *
                    100.0f);
  return buffer;
}

std::string fontSelectionText(
    const GameSettings& settings,
    const std::vector<std::string>& availableFonts) {
  if (availableFonts.empty()) {
    return "No Fonts Found";
  }
  return settings.fontDisplayName();
}

std::string resolutionDisplayText(const glm::ivec2& resolution) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%d x %d", resolution.x, resolution.y);
  return buffer;
}

const char* onOffText(bool enabled) { return enabled ? "On" : "Off"; }

bool sameResolution(const glm::ivec2& a, const glm::ivec2& b) {
  return a.x == b.x && a.y == b.y;
}

bool sameFloat(float a, float b, float epsilon) {
  return std::fabs(a - b) <= epsilon;
}

bool sameAudioSettings(const ENGINE::AUDIO::AudioSettings& a,
                       const ENGINE::AUDIO::AudioSettings& b) {
  if (a.musicEnabled != b.musicEnabled || a.globalMute != b.globalMute) {
    return false;
  }

  for (ENGINE::AUDIO::SoundCategoryId category :
       ENGINE::AUDIO::kSoundCategoryOrder) {
    if (!sameFloat(a.volumeFor(category), b.volumeFor(category)) ||
        a.categoryMuted[ENGINE::AUDIO::toIndex(category)] !=
            b.categoryMuted[ENGINE::AUDIO::toIndex(category)]) {
      return false;
    }
  }

  return true;
}

bool sameControlOverrides(const InputMapping::ControlBindingOverrides& a,
                         const InputMapping::ControlBindingOverrides& b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (const auto& [actionId, binding] : a) {
    const auto otherIt = b.find(actionId);
    if (otherIt == b.end() ||
        !InputMapping::sameBinding(binding, otherIt->second)) {
      return false;
    }
  }

  return true;
}

bool sameGameSettings(const GameSettings& a, const GameSettings& b) {
  return a.fontFile == b.fontFile &&
         sameResolution(a.resolution, b.resolution) &&
         a.renderDistance == b.renderDistance &&
         sameFloat(a.mouseSensitivity, b.mouseSensitivity) &&
         sameFloat(a.renderScale, b.renderScale) &&
         a.windowMode == b.windowMode &&
         a.vSyncEnabled == b.vSyncEnabled &&
         a.showFpsCounterOnly == b.showFpsCounterOnly &&
         sameAudioSettings(a.audioSettings, b.audioSettings) &&
         sameControlOverrides(a.controlOverrides, b.controlOverrides);
}

bool hasPendingSettingsChanges(const GameSettings& appliedSettings,
                               const GameSettings& pendingSettings) {
  return !sameGameSettings(appliedSettings, pendingSettings);
}

glm::ivec2 recommendedMonitorResolution() {
  const glm::vec2 monitorSize = ENGINE::GETMONITORSIZE();
  if (monitorSize.x <= 0.0f || monitorSize.y <= 0.0f) {
    return glm::ivec2(0);
  }

  return glm::ivec2(static_cast<int>(monitorSize.x + 0.5f),
                    static_cast<int>(monitorSize.y + 0.5f));
}

std::string resolutionSelectionText(
    const GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions,
    bool locked) {
  if (locked) {
    const glm::vec2 monitorSize = ENGINE::GETMONITORSIZE();
    if (monitorSize.x > 0.0f && monitorSize.y > 0.0f) {
      return resolutionDisplayText(glm::ivec2(
          static_cast<int>(monitorSize.x + 0.5f),
          static_cast<int>(monitorSize.y + 0.5f))) + " (Locked)";
    }
    return resolutionDisplayText(settings.resolution) + " (Locked)";
  }

  if (availableResolutions.empty()) {
    return resolutionDisplayText(settings.resolution);
  }

  std::string text = resolutionDisplayText(settings.resolution);
  const glm::ivec2 recommended = recommendedMonitorResolution();
  if (recommended.x > 0 && recommended.y > 0 &&
      sameResolution(settings.resolution, recommended)) {
    text += " (Active)";
  }
  return text;
}

void applyWindowSettings(RuntimeUiState& uiState, const GameSettings& settings) {
  const glm::ivec2 viewportSize =
      RuntimeUI::appliedViewportSizeForSettings(settings);
  const glm::vec2 viewportSizeVec(static_cast<float>(viewportSize.x),
                                  static_cast<float>(viewportSize.y));
  ENGINE::SETDEFAULTVIEWPORTSIZE(viewportSizeVec);
  ENGINE::SETVIEWPORTSIZE(viewportSizeVec);

  if (settings.windowMode != ENGINE::VIEWPORTMODE::FULLSCREEN) {
    uiState.lastNonFullscreenMode = settings.windowMode;
  }

  ENGINE::SETVIEWPORTMODE(settings.windowMode);
}

void stepRenderDistanceSelection(GameSettings& settings, int delta) {
  constexpr WorldStack::RenderDistancePreset presets[] = {
      WorldStack::RenderDistancePreset::SHORT,
      WorldStack::RenderDistancePreset::NORMAL,
      WorldStack::RenderDistancePreset::FAR,
      WorldStack::RenderDistancePreset::ULTRA,
  };

  int currentIndex = 0;
  for (int index = 0; index < 4; index++) {
    if (presets[index] == settings.renderDistance) {
      currentIndex = index;
      break;
    }
  }

  currentIndex = (currentIndex + (delta % 4) + 4) % 4;
  settings.renderDistance = presets[currentIndex];
}

void stepMouseSensitivitySelection(GameSettings& settings, float delta) {
  settings.mouseSensitivity =
      glm::clamp(settings.mouseSensitivity + delta,
                 ClientDefaults::kMinMouseSensitivity,
                 ClientDefaults::kMaxMouseSensitivity);
}

void stepRenderScaleSelection(GameSettings& settings, float delta) {
  settings.renderScale =
      glm::clamp(settings.renderScale + delta, ClientDefaults::kMinRenderScale,
                 ClientDefaults::kMaxRenderScale);
}

int currentResolutionIndex(
    const GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions) {
  if (availableResolutions.empty()) {
    return -1;
  }

  for (int index = 0; index < static_cast<int>(availableResolutions.size());
       index++) {
    if (sameResolution(availableResolutions[static_cast<std::size_t>(index)],
                       settings.resolution)) {
      return index;
    }
  }
  return 0;
}

void stepResolutionSelection(
    GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions,
    int delta) {
  if (availableResolutions.empty()) {
    return;
  }

  int index = currentResolutionIndex(settings, availableResolutions);
  if (index < 0) {
    index = 0;
  }
  const int count = static_cast<int>(availableResolutions.size());
  index = (index + (delta % count) + count) % count;
  settings.resolution = availableResolutions[static_cast<std::size_t>(index)];
}

int currentFontIndex(const GameSettings& settings,
                     const std::vector<std::string>& availableFonts) {
  if (availableFonts.empty()) {
    return -1;
  }

  auto normalize = [](std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return value;
  };

  const std::string selected = normalize(settings.fontFile);
  for (int index = 0; index < static_cast<int>(availableFonts.size()); index++) {
    if (normalize(availableFonts[static_cast<std::size_t>(index)]) == selected) {
      return index;
    }
  }
  return 0;
}

void stepFontSelection(GameSettings& settings,
                       const std::vector<std::string>& availableFonts,
                       int delta) {
  if (availableFonts.empty()) {
    return;
  }

  int index = currentFontIndex(settings, availableFonts);
  if (index < 0) {
    index = 0;
  }
  const int count = static_cast<int>(availableFonts.size());
  index = (index + (delta % count) + count) % count;
  settings.fontFile = availableFonts[static_cast<std::size_t>(index)];
}

void stepWindowModeSelection(GameSettings& settings, int delta) {
  constexpr ENGINE::VIEWPORTMODE modes[] = {
      ENGINE::VIEWPORTMODE::WINDOWMODE,
      ENGINE::VIEWPORTMODE::BORDERLESS,
      ENGINE::VIEWPORTMODE::FULLSCREEN,
  };

  int currentIndex = 0;
  for (int index = 0; index < 3; index++) {
    if (modes[index] == settings.windowMode) {
      currentIndex = index;
      break;
    }
  }

  currentIndex = (currentIndex + (delta % 3) + 3) % 3;
  settings.windowMode = modes[currentIndex];
}

void toggleVSyncSelection(GameSettings& settings) {
  settings.vSyncEnabled = !settings.vSyncEnabled;
}

void toggleFpsCounterOnlySelection(GameSettings& settings) {
  settings.showFpsCounterOnly = !settings.showFpsCounterOnly;
}

std::string audioVolumeText(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f%%",
                glm::clamp(value, 0.0f, 1.0f) * 100.0f);
  return buffer;
}

const char* audioCategoryDisplayName(ENGINE::AUDIO::SoundCategoryId category) {
  switch (category) {
    case ENGINE::AUDIO::SoundCategoryId::Master:
      return "Master Volume";
    case ENGINE::AUDIO::SoundCategoryId::Music:
      return "Music Volume";
    case ENGINE::AUDIO::SoundCategoryId::Ambient:
      return "Ambient Volume";
    case ENGINE::AUDIO::SoundCategoryId::Block:
      return "Block Volume";
    case ENGINE::AUDIO::SoundCategoryId::Player:
      return "Player Volume";
    case ENGINE::AUDIO::SoundCategoryId::Hostile:
      return "Hostile Volume";
    case ENGINE::AUDIO::SoundCategoryId::Neutral:
      return "Neutral Volume";
    case ENGINE::AUDIO::SoundCategoryId::Weather:
      return "Weather Volume";
    case ENGINE::AUDIO::SoundCategoryId::Ui:
      return "UI Volume";
    default:
      return "Volume";
  }
}

void stepAudioCategoryVolumeSelection(
    GameSettings& settings,
    ENGINE::AUDIO::SoundCategoryId category,
    float delta) {
  float& volume =
      settings.audioSettings.categoryVolumes[ENGINE::AUDIO::toIndex(category)];
  volume = glm::clamp(volume + delta, 0.0f, 1.0f);
}

void toggleMusicEnabledSelection(GameSettings& settings) {
  settings.audioSettings.musicEnabled = !settings.audioSettings.musicEnabled;
}

void toggleGlobalMuteSelection(GameSettings& settings) {
  settings.audioSettings.globalMute = !settings.audioSettings.globalMute;
}

bool applyPendingSettings(Player& player, WorldStack& worldStack,
                          Renderer& renderer,
                          GameAudioController& audioController,
                          RuntimeUiState& uiState,
                          GameSettings& appliedSettings,
                          GameSettings& pendingSettings) {
  if (!hasPendingSettingsChanges(appliedSettings, pendingSettings)) {
    return false;
  }

  const bool fontChanged = appliedSettings.fontFile != pendingSettings.fontFile;
  const bool mouseChanged =
      !sameFloat(appliedSettings.mouseSensitivity, pendingSettings.mouseSensitivity);
  const bool renderScaleChanged =
      !sameFloat(appliedSettings.renderScale, pendingSettings.renderScale);
  const bool windowSettingsChanged =
      appliedSettings.windowMode != pendingSettings.windowMode ||
      !sameResolution(appliedSettings.resolution, pendingSettings.resolution);
  const bool audioChanged =
      !sameAudioSettings(appliedSettings.audioSettings, pendingSettings.audioSettings);
  const bool controlsChanged =
      !sameControlOverrides(appliedSettings.controlOverrides,
                            pendingSettings.controlOverrides);

  if (appliedSettings.renderDistance != pendingSettings.renderDistance) {
    worldStack.setRenderDistancePreset(pendingSettings.renderDistance);
    std::printf("[Settings] Render distance: %s\n",
                renderDistanceDisplayName(pendingSettings.renderDistance));
  }

  if (mouseChanged) {
    player.camera.sensitivity = pendingSettings.mouseSensitivity;
    std::printf("[Settings] Mouse sensitivity: %.4f\n",
                pendingSettings.mouseSensitivity);
  }

  if (renderScaleChanged) {
    renderer.setRenderScale(pendingSettings.renderScale);
    std::printf("[Settings] Render scale: %s\n",
                renderScaleText(pendingSettings.renderScale).c_str());
  }

  if (windowSettingsChanged) {
    applyWindowSettings(uiState, pendingSettings);
    std::printf("[Settings] Window mode: %s\n",
                Bootstrap::viewportModeName(pendingSettings.windowMode));
    std::printf("[Settings] Resolution: %s\n",
                resolutionSelectionText(
                    pendingSettings, {}, pendingSettings.windowMode ==
                                           ENGINE::VIEWPORTMODE::BORDERLESS)
                    .c_str());
  }

  if (appliedSettings.vSyncEnabled != pendingSettings.vSyncEnabled) {
    ENGINE::SETVSYNC(pendingSettings.vSyncEnabled);
    std::printf("[Settings] VSync: %s\n", onOffText(pendingSettings.vSyncEnabled));
  }

  if (appliedSettings.showFpsCounterOnly != pendingSettings.showFpsCounterOnly) {
    std::printf("[Settings] FPS counter only: %s\n",
                onOffText(pendingSettings.showFpsCounterOnly));
  }

  if (audioChanged) {
    audioController.applySettings(pendingSettings.audioSettings);
    std::printf("[Settings] Master volume: %s\n",
                audioVolumeText(
                    pendingSettings.audioSettings.volumeFor(
                        ENGINE::AUDIO::SoundCategoryId::Master))
                    .c_str());
    std::printf("[Settings] Music enabled: %s\n",
                onOffText(pendingSettings.audioSettings.musicEnabled));
    std::printf("[Settings] Global mute: %s\n",
                onOffText(pendingSettings.audioSettings.globalMute));
  }

  if (controlsChanged) {
    InputMapping::InputActionSystem::instance().applyOverrides(
        pendingSettings.controlOverrides);
    std::printf("[Settings] Controls updated: %zu override(s)\n",
                pendingSettings.controlOverrides.size());
  }

  if (fontChanged) {
    std::printf("[Settings] HUD font: %s\n", pendingSettings.fontFile.c_str());
  }

  appliedSettings = pendingSettings;
  RuntimeUI::saveGameSettings(appliedSettings);

  if (fontChanged) {
    uiState.hudRebuildRequested = true;
  }
  return true;
}

void openSettingsMenu(RuntimeUiState& uiState,
                      const GameSettings& appliedSettings,
                      GameSettings& pendingSettings) {
  pendingSettings = appliedSettings;
  uiState.settingsMenuOpen = true;
  uiState.settingsDiscardConfirmOpen = false;
  uiState.activeSettingsTab = RuntimeUI::SettingsMenuTab::General;
  resetControlsMenuState(uiState);
}

void discardPendingSettings(RuntimeUiState& uiState,
                            const GameSettings& appliedSettings,
                            GameSettings& pendingSettings) {
  pendingSettings = appliedSettings;
  uiState.settingsDiscardConfirmOpen = false;
  resetControlsMenuState(uiState);
}

void closeSettingsMenu(RuntimeUiState& uiState,
                       const GameSettings& appliedSettings,
                       GameSettings& pendingSettings) {
  discardPendingSettings(uiState, appliedSettings, pendingSettings);
  uiState.settingsMenuOpen = false;
}

void requestCloseSettingsMenu(RuntimeUiState& uiState,
                              const GameSettings& appliedSettings,
                              GameSettings& pendingSettings) {
  if (hasPendingSettingsChanges(appliedSettings, pendingSettings)) {
    uiState.settingsDiscardConfirmOpen = true;
    return;
  }
  closeSettingsMenu(uiState, appliedSettings, pendingSettings);
}

void toggleFullscreen(RuntimeUiState& uiState, GameSettings& settings) {
  settings.windowMode =
      settings.windowMode == ENGINE::VIEWPORTMODE::FULLSCREEN
          ? uiState.lastNonFullscreenMode
          : ENGINE::VIEWPORTMODE::FULLSCREEN;
  applyWindowSettings(uiState, settings);
  RuntimeUI::saveGameSettings(settings);
  std::printf("[Settings] Window mode: %s\n",
              Bootstrap::viewportModeName(settings.windowMode));
}

std::string settingsSubtitleText(const GameSettings& appliedSettings,
                                 const GameSettings& pendingSettings) {
  return hasPendingSettingsChanges(appliedSettings, pendingSettings)
             ? "Press Apply to save changes"
             : "No pending changes";
}

hudButtonText::Positioner makeCenteredMenuButtonPositioner(float yOffset,
                                                           float xOffset) {
  return [xOffset, yOffset](int screenWidth, int screenHeight, glm::vec2 size) {
    return glm::ivec2(static_cast<int>((screenWidth - size.x) * 0.5f + xOffset),
                      static_cast<int>(screenHeight * 0.5f + yOffset));
  };
}

hudButtonText::Positioner makeBottomMenuButtonPositioner(float bottomMargin,
                                                         float xOffset) {
  return [xOffset, bottomMargin](int screenWidth, int screenHeight, glm::vec2 size) {
    return glm::ivec2(
        static_cast<int>((screenWidth - size.x) * 0.5f + xOffset),
        static_cast<int>(screenHeight - bottomMargin - size.y));
  };
}

HUDLayout makeSettingsLabelLayout(float yOffset, float columnOffset) {
  return makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER_RIGHT,
                       glm::vec2(columnOffset - 120.0f, yOffset));
}

HUDLayout makeSettingsValueLayout(float yOffset, float columnOffset) {
  return makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER,
                       glm::vec2(columnOffset + 120.0f, yOffset));
}

}  // namespace RuntimeUI::Detail

namespace RuntimeUI {

glm::ivec2 appliedViewportSizeForSettings(const GameSettings& settings) {
  if (settings.windowMode == ENGINE::VIEWPORTMODE::BORDERLESS) {
    const glm::vec2 monitorSize = ENGINE::GETMONITORSIZE();
    if (monitorSize.x > 0.0f && monitorSize.y > 0.0f) {
      return glm::ivec2(static_cast<int>(monitorSize.x + 0.5f),
                        static_cast<int>(monitorSize.y + 0.5f));
    }
  }

  return settings.resolution;
}

void saveGameSettings(const GameSettings& settings) {
  std::string error;
  if (!settings.save(&error)) {
    std::printf("[Settings][ERROR] %s\n", error.c_str());
  }
}

void triggerSaveToast(RuntimeUiState& uiState, const std::string& message) {
  uiState.saveToastText = message.empty() ? "Game Saved" : message;
  uiState.saveToastTimer = uiState.saveToastDuration;
}

void updateSaveToast(RuntimeUiState& uiState, float dt) {
  if (uiState.saveToastTimer <= 0.0f) {
    uiState.saveToastTimer = 0.0f;
    return;
  }

  uiState.saveToastTimer = std::max(0.0f, uiState.saveToastTimer - dt);
}

}  // namespace RuntimeUI
