#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "client_defaults.hpp"
#include "engine/engine.hpp"

struct GLFWwindow;
struct GameSettings;

class GameAudioController;
class Player;
class Renderer;
class WorldStack;
class hudPortalInfo;
class hudPortalTracker;

namespace RuntimeUI {

enum class SettingsMenuTab : std::uint8_t {
  General = 0,
  Video,
  Sound,
  Controls,
};

struct RuntimeUiState {
  bool settingsMenuOpen = false;
  bool settingsDiscardConfirmOpen = false;
  bool hudRebuildRequested = false;
  bool returnToLauncherRequested = false;
  bool debugTextVisible = false;
  std::string saveToastText = "Game Saved";
  float saveToastTimer = 0.0f;
  float saveToastDuration = 1.75f;
  float saveToastFadeInDuration = 0.18f;
  float saveToastFadeOutDuration = 0.32f;
  SettingsMenuTab activeSettingsTab = SettingsMenuTab::General;
  // Cached mode and resolution restored when leaving windowed mode.
  ENGINE::VIEWPORTMODE lastNonFullscreenMode = ENGINE::VIEWPORTMODE::BORDERLESS;
  glm::ivec2 lastNonFullscreenResolution{ClientDefaults::kDefaultWindowedResolution};
  std::size_t controlsCategoryIndex = 0;
  bool controlsCaptureOpen = false;
  bool controlsCaptureIgnoreMouseLeft = false;
  std::string controlsCaptureActionId{};
  std::string controlsWarningMessage{};
};

glm::ivec2 appliedViewportSizeForSettings(const GameSettings& settings);
void saveGameSettings(const GameSettings& settings);

void syncHudMenuState(RuntimeUiState& uiState);
void syncDebugHudState(const RuntimeUiState& uiState,
                       const GameSettings& settings);
void syncSaveToastState(const RuntimeUiState& uiState);
void syncCursorVisibility(const Player& player,
                          const hudPortalTracker* portalTracker = nullptr,
                          bool chatOpen = false);
void triggerSaveToast(RuntimeUiState& uiState,
                      const std::string& message = "Game Saved");
void updateSaveToast(RuntimeUiState& uiState, float dt);

hudPortalInfo* setupHUD(Player& player, WorldStack& worldStack, Renderer& renderer,
                        GameAudioController& audioController,
                        GLFWwindow* window, GameSettings& appliedSettings,
                        GameSettings& pendingSettings,
                        const std::vector<std::string>& availableFonts,
                        const std::vector<glm::ivec2>& availableResolutions,
                        RuntimeUiState& uiState,
                        hudPortalTracker** outPortalTracker = nullptr);

void handleGlobalShortcuts(hudPortalInfo* portalInfo,
                           hudPortalTracker* portalTracker,
                           WorldStack& worldStack,
                           Player& player, GameAudioController& audioController,
                           RuntimeUiState& uiState,
                           GameSettings& appliedSettings,
                           GameSettings& pendingSettings);

bool shouldRenderDeveloperUi();
void drawDeveloperUi(const WorldStack& worldStack, const Player& player);

} // namespace RuntimeUI
