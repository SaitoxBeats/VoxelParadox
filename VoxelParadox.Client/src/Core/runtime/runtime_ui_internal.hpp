#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "audio/game_audio_controller.hpp"
#include "engine/input.hpp"
#include "player/player.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_button_text.hpp"
#include "render/hud/hud_hotbar.hpp"
#include "render/hud/hud_image.hpp"
#include "render/hud/hud_inventory_menu.hpp"
#include "render/hud/hud_panel.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "render/hud/hud_slider.hpp"
#include "render/hud/hud_text.hpp"
#include "render/hud/hud_watch_text.hpp"
#include "render/renderer.hpp"
#include "runtime/game_settings.hpp"
#include "runtime/runtime_ui.hpp"
#include "world/world_stack.hpp"

namespace RuntimeUI::Detail {

// Shared private helpers used by the split RuntimeUI implementation files.
template <typename T>
T* attachToHUDGroup(T* element, const char* groupName, int layer = 0) {
  if (element) {
    element->setLayer(layer);
    HUD::assignToGroup(element, groupName);
  }
  return element;
}

const char* renderDistanceDisplayName(WorldStack::RenderDistancePreset preset);
std::string mouseSensitivityText(float sensitivity);
std::string renderScaleText(float renderScale);
std::string fontSelectionText(const GameSettings& settings,
                              const std::vector<std::string>& availableFonts);
std::string resolutionDisplayText(const glm::ivec2& resolution);
const char* onOffText(bool enabled);

bool sameResolution(const glm::ivec2& a, const glm::ivec2& b);
bool sameFloat(float a, float b, float epsilon = 0.000001f);
bool sameAudioSettings(const ENGINE::AUDIO::AudioSettings& a,
                       const ENGINE::AUDIO::AudioSettings& b);
bool sameGameSettings(const GameSettings& a, const GameSettings& b);
bool hasPendingSettingsChanges(const GameSettings& appliedSettings,
                               const GameSettings& pendingSettings);

glm::ivec2 recommendedMonitorResolution();
std::string resolutionSelectionText(
    const GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions,
    bool locked = false);

void applyWindowSettings(RuntimeUiState& uiState, const GameSettings& settings);
void stepRenderDistanceSelection(GameSettings& settings, int delta);
void stepMouseSensitivitySelection(GameSettings& settings, float delta);
void stepRenderScaleSelection(GameSettings& settings, float delta);
int currentResolutionIndex(
    const GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions);
void stepResolutionSelection(
    GameSettings& settings,
    const std::vector<glm::ivec2>& availableResolutions,
    int delta);
int currentFontIndex(const GameSettings& settings,
                     const std::vector<std::string>& availableFonts);
void stepFontSelection(GameSettings& settings,
                       const std::vector<std::string>& availableFonts,
                       int delta);
void stepWindowModeSelection(GameSettings& settings, int delta);
void toggleVSyncSelection(GameSettings& settings);
void toggleFpsCounterOnlySelection(GameSettings& settings);

std::string audioVolumeText(float value);
const char* audioCategoryDisplayName(ENGINE::AUDIO::SoundCategoryId category);
void stepAudioCategoryVolumeSelection(
    GameSettings& settings,
    ENGINE::AUDIO::SoundCategoryId category,
    float delta);
void toggleMusicEnabledSelection(GameSettings& settings);
void toggleGlobalMuteSelection(GameSettings& settings);

bool applyPendingSettings(Player& player, WorldStack& worldStack,
                          Renderer& renderer,
                          GameAudioController& audioController,
                          RuntimeUiState& uiState,
                          GameSettings& appliedSettings,
                          GameSettings& pendingSettings);
void openSettingsMenu(RuntimeUiState& uiState,
                      const GameSettings& appliedSettings,
                      GameSettings& pendingSettings);
void discardPendingSettings(RuntimeUiState& uiState,
                            const GameSettings& appliedSettings,
                            GameSettings& pendingSettings);
void closeSettingsMenu(RuntimeUiState& uiState,
                       const GameSettings& appliedSettings,
                       GameSettings& pendingSettings);
void requestCloseSettingsMenu(RuntimeUiState& uiState,
                              const GameSettings& appliedSettings,
                              GameSettings& pendingSettings);
void toggleFullscreen(RuntimeUiState& uiState, GameSettings& settings);
std::string settingsSubtitleText(const GameSettings& appliedSettings,
                                 const GameSettings& pendingSettings);

hudButtonText::Positioner makeCenteredMenuButtonPositioner(float yOffset,
                                                           float xOffset = 0.0f);
hudButtonText::Positioner makeBottomMenuButtonPositioner(float bottomMargin,
                                                         float xOffset = 0.0f);
HUDLayout makeSettingsLabelLayout(float yOffset, float columnOffset = 0.0f);
HUDLayout makeSettingsValueLayout(float yOffset, float columnOffset = 0.0f);

void createHUDGroups();
void addFpsOnlyHUD();
void addDebugHUD(WorldStack& worldStack, Player& player);
void addPlayerStatusHUD(Player& player);
void addHotbarHUD(Player& player, Renderer& renderer, WorldStack& worldStack);
void addSaveToastHUD(RuntimeUiState& uiState);
void addPauseMenuHUD(GLFWwindow* window, RuntimeUiState& uiState,
                     const GameSettings& appliedSettings,
                     GameSettings& pendingSettings);
void addSettingsMenuHUD(
    Player& player, WorldStack& worldStack, Renderer& renderer,
    GameAudioController& audioController,
    GameSettings& appliedSettings, GameSettings& pendingSettings,
    const std::vector<std::string>& availableFonts,
    const std::vector<glm::ivec2>& availableResolutions,
    RuntimeUiState& uiState);
void addSettingsDiscardConfirmHUD(
    Player& player, WorldStack& worldStack, Renderer& renderer,
    GameAudioController& audioController,
    GameSettings& appliedSettings, GameSettings& pendingSettings,
    RuntimeUiState& uiState);
void addSettingsControlsCaptureHUD(RuntimeUiState& uiState);
void addInventoryMenuHUD(Player& player, Renderer& renderer,
                         WorldStack& worldStack);
void addPortalTrackerHUD(Player& player, WorldStack& worldStack,
                         hudPortalTracker*& outPortalTracker);
void togglePauseOrCancelPortalEdit(hudPortalInfo* portalInfo);

}  // namespace RuntimeUI::Detail
