// runtime_app_loop.cpp
// Per-frame runtime flow: input, gameplay, audio, rendering, and shutdown.

#include "runtime_app_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// External
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// Solution dependencies
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"

// Client
#include "audio/game_audio_controller.hpp"
#include "client_defaults.hpp"
#include "debug/biome_debug_tools.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"
#include "player/player.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "render/hud/hud_text.hpp"
#include "render/renderer.hpp"
#include "runtime/game_chat.hpp"
#include "runtime/runtime_hud_ids.hpp"
#include "world/world_save_service.hpp"
#include "runtime/runtime_ui.hpp"
#include "ui/biome_teleport_window.hpp"
#include "ui/imgui_layer.hpp"
#include "world/biome_registry.hpp"
#include "world/world_stack.hpp"

namespace {

struct DeathSequenceState {
  static constexpr float kDurationSeconds = 20.0f;
  static constexpr float kTextFadeDurationSeconds = 3.0f;
  static constexpr float kVignetteFadeDurationSeconds = 5.0f;

  bool active = false;
  bool paused = false;
  float elapsedSeconds = 0.0f;
  double pausedRenderTimeSeconds = 0.0;
  std::string message{};
  const std::vector<std::string>* messages = nullptr;
  std::size_t messageIndex = 0;
  hudText* messageText = nullptr;
};

std::filesystem::path motivationalMessagesPath() {
  return AppPaths::resolve(ClientAssets::kMotivationalMessagesFile);
}

std::filesystem::path motivationalMessagesTemplatePath() {
  return AppPaths::resolve(ClientAssets::kMotivationalMessagesTemplateFile);
}

std::string trimMotivationalLine(std::string line);

std::vector<std::string> defaultMotivationalMessagesFallback() {
  return {
      "Stand up again. The next step can change everything.",
      "Keep breathing. The next attempt can still become the right one.",
      "Rest for a moment, then return stronger than before.",
  };
}

std::vector<std::string> readMotivationalMessagesFile(
    const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return {};
  }

  std::vector<std::string> messages;
  std::string line;
  while (std::getline(file, line)) {
    line = trimMotivationalLine(std::move(line));
    if (!line.empty()) {
      messages.push_back(std::move(line));
    }
  }

  return messages;
}

bool writeMotivationalMessagesFile(const std::filesystem::path& path,
                                   const std::vector<std::string>& lines) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  for (std::size_t index = 0; index < lines.size(); ++index) {
    file << lines[index];
    if (index + 1 < lines.size()) {
      file << '\n';
    }
  }

  file << '\n';
  return static_cast<bool>(file);
}

bool copyMotivationalMessagesTemplate(const std::filesystem::path& destination) {
  const std::filesystem::path templatePath = motivationalMessagesTemplatePath();
  std::error_code ec;
  if (!std::filesystem::exists(templatePath, ec)) {
    return false;
  }

  std::filesystem::create_directories(destination.parent_path(), ec);
  ec.clear();
  std::filesystem::copy_file(
      templatePath, destination, std::filesystem::copy_options::overwrite_existing,
      ec);
  return !ec;
}

void stripUtf8Bom(std::string& line) {
  if (line.size() >= 3 &&
      static_cast<unsigned char>(line[0]) == 0xEF &&
      static_cast<unsigned char>(line[1]) == 0xBB &&
      static_cast<unsigned char>(line[2]) == 0xBF) {
    line.erase(0, 3);
  }
}

std::string trimMotivationalLine(std::string line) {
  stripUtf8Bom(line);

  const std::size_t begin = line.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }

  const std::size_t end = line.find_last_not_of(" \t\r\n");
  return line.substr(begin, end - begin + 1);
}

std::vector<std::string> loadMotivationalMessages() {
  const std::filesystem::path path = motivationalMessagesPath();
  std::vector<std::string> messages = readMotivationalMessagesFile(path);
  if (!messages.empty()) {
    return messages;
  }

  messages = readMotivationalMessagesFile(motivationalMessagesTemplatePath());
  if (!messages.empty()) {
    if (!copyMotivationalMessagesTemplate(path)) {
      writeMotivationalMessagesFile(path, messages);
    }
    return messages;
  }

  messages = defaultMotivationalMessagesFallback();
  writeMotivationalMessagesFile(path, messages);
  return messages;
}

const std::vector<std::string>& motivationalMessages() {
  static const std::vector<std::string> messages = loadMotivationalMessages();
  return messages;
}

float deathScreenTextOpacity(float elapsedSeconds);

std::size_t randomMotivationalMessageIndex(
    const std::vector<std::string>& messages) {
  if (messages.empty()) {
    return 0;
  }

  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<std::size_t> distribution(0, messages.size() - 1);
  return distribution(rng);
}

std::string randomMotivationalMessage() {
  const std::vector<std::string>& messages = motivationalMessages();
  if (messages.empty()) {
    return defaultMotivationalMessagesFallback().front();
  }

  return messages[randomMotivationalMessageIndex(messages)];
}

void updateDeathScreenMessage(DeathSequenceState& deathState, Player& player) {
  if (!deathState.messages || deathState.messages->empty()) {
    deathState.message.clear();
    if (deathState.messageText) {
      deathState.messageText->setText("");
      deathState.messageText->setOpacity(
          deathScreenTextOpacity(deathState.elapsedSeconds));
    }
    player.setDeathSequenceState(true, deathState.elapsedSeconds, deathState.message);
    return;
  }

  if (deathState.messageIndex >= deathState.messages->size()) {
    deathState.messageIndex = 0;
  }

  deathState.message = deathState.messages->at(deathState.messageIndex);
  if (deathState.messageText) {
    deathState.messageText->setText(deathState.message);
    deathState.messageText->setOpacity(
        deathScreenTextOpacity(deathState.elapsedSeconds));
  }
  player.setDeathSequenceState(true, deathState.elapsedSeconds, deathState.message);
}

float deathScreenTextOpacity(float elapsedSeconds) {
  const float fadeIn = glm::clamp(
      elapsedSeconds / DeathSequenceState::kTextFadeDurationSeconds, 0.0f, 1.0f);
  const float remaining =
      DeathSequenceState::kDurationSeconds - elapsedSeconds;
  const float fadeOut = glm::clamp(
      remaining / DeathSequenceState::kTextFadeDurationSeconds, 0.0f, 1.0f);
  return glm::min(fadeIn, fadeOut);
}

float deathScreenVignetteExtra(float elapsedSeconds) {
  if (elapsedSeconds <= DeathSequenceState::kVignetteFadeDurationSeconds) {
    return glm::clamp(
        1.0f - elapsedSeconds / DeathSequenceState::kVignetteFadeDurationSeconds,
        0.0f, 1.0f);
  }

  const float fadeOutStart =
      DeathSequenceState::kDurationSeconds -
      DeathSequenceState::kVignetteFadeDurationSeconds;
  if (elapsedSeconds >= fadeOutStart) {
    return glm::clamp(
        (elapsedSeconds - fadeOutStart) /
            DeathSequenceState::kVignetteFadeDurationSeconds,
        0.0f, 1.0f);
  }

  return 0.0f;
}

bool deathScreenMusicFadeOutActive(float elapsedSeconds) {
  return elapsedSeconds >=
         (DeathSequenceState::kDurationSeconds -
          DeathSequenceState::kVignetteFadeDurationSeconds);
}

bool canCaptureGameplayScreenshot(const Player& player, const GameChat& gameChat,
                                  hudPortalInfo* portalInfo,
                                  hudPortalTracker* portalTracker) {
  return !ENGINE::ISPAUSED() && !Input::hasUiFocus() && !player.isInventoryOpen() &&
         !gameChat.isOpen() && player.transition == PlayerTransition::NONE &&
         (!portalInfo || !portalInfo->isEditing()) &&
         (!portalTracker || !portalTracker->isMenuOpen());
}

std::filesystem::path makeScreenshotPath() {
  std::filesystem::path screenshotDir = AppPaths::savesRoot() / "Screenshots";
  const std::string& saveDirectory = WorldStack::getSaveWorldDirectory();
  if (!saveDirectory.empty()) {
    const std::filesystem::path universesDirectory(saveDirectory);
    const std::filesystem::path worldDirectory = universesDirectory.parent_path();
    if (!worldDirectory.empty()) {
      screenshotDir = worldDirectory / "screenshots";
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(screenshotDir, ec);
  if (ec) {
    return {};
  }

  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
  std::tm localTime{};
  localtime_s(&localTime, &nowTime);

  std::ostringstream filename;
  filename << "screenshot_" << std::put_time(&localTime, "%Y%m%d_%H%M%S");

  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
      1000;
  filename << "_" << std::setw(3) << std::setfill('0') << millis.count() << ".png";
  return screenshotDir / filename.str();
}

bool captureGameplayScreenshot(GLFWwindow* window) {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (width <= 0 || height <= 0) {
    return false;
  }

  const std::filesystem::path screenshotPath = makeScreenshotPath();
  if (screenshotPath.empty()) {
    return false;
  }

  std::vector<unsigned char> pixels(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 4u);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // OpenGL reads bottom-to-top. Flip rows so the PNG matches the on-screen view.
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
  std::vector<unsigned char> flipped(pixels.size());
  for (int y = 0; y < height; ++y) {
    const std::size_t srcOffset =
        static_cast<std::size_t>(height - 1 - y) * rowBytes;
    const std::size_t dstOffset = static_cast<std::size_t>(y) * rowBytes;
    std::copy_n(pixels.data() + srcOffset, rowBytes, flipped.data() + dstOffset);
  }

  const int writeOk = stbi_write_png(screenshotPath.string().c_str(), width, height, 4,
                                     flipped.data(), width * 4);
  if (writeOk == 0) {
    return false;
  }

  std::printf("[Screenshot] Saved to %s\n", screenshotPath.string().c_str());
  return true;
}

bool saveCurrentWorldSession(WorldSaveService::WorldSession& worldSession,
                             Player& player, WorldStack& worldStack,
                             hudPortalTracker* portalTracker,
                             RuntimeUI::RuntimeUiState& uiState,
                             double totalPlaytimeSeconds, bool showToast,
                             const Player::PersistentState* overriddenPlayerState = nullptr,
                             std::string* outError = nullptr) {
  if (portalTracker) {
    worldSession.playerData.hasPortalTrackerState = true;
    worldSession.playerData.portalTrackerState =
        portalTracker->capturePersistentState();
  } else {
    worldSession.playerData.hasPortalTrackerState = false;
    worldSession.playerData.portalTrackerState = {};
  }

  const bool saved = overriddenPlayerState
                         ? WorldSaveService::saveSessionWithPlayerState(
                               worldSession, *overriddenPlayerState,
                               player.camera.position, player.camera.orientation,
                               worldStack, totalPlaytimeSeconds, outError)
                         : WorldSaveService::saveSession(worldSession, player, worldStack,
                                                         totalPlaytimeSeconds, outError);
  if (!saved) {
    return false;
  }

  worldSession.totalPlaytimeSeconds = totalPlaytimeSeconds;
  if (showToast) {
    RuntimeUI::triggerSaveToast(uiState);
  }
  return true;
}

bool applySpawnWaitingState(Player::PersistentState& state, Player& player,
                            WorldStack& worldStack, bool restoreLifePoints,
                            std::string* outError = nullptr) {
  glm::vec3 spawnPosition = worldStack.resolveCurrentWorldSpawnPosition(
      ClientDefaults::kPlayerSpawnPosition, Player::kDefaultPlayerRadius,
      Player::kDefaultStandingHeight, Player::kDefaultStandingEyeHeight);

  if (state.hasSpawnpoint) {
    spawnPosition = state.spawnpointPosition;
  }

  state.cameraPosition = spawnPosition;
  state.velocity = glm::vec3(0.0f);
  state.lifePoints =
      restoreLifePoints ? player.getMaxLifePoints() : 0;
  state.grounded = false;
  state.crouching = false;
  state.currentEyeHeight = player.getStandingEyeHeight();
  state.headBobBlend = 0.0f;
  state.headBobLocalOffset = glm::vec3(0.0f);
  state.headBobRollRadians = 0.0f;
  state.damageRollTimer = 0.0f;
  state.damageRollRadiansCurrent = 0.0f;
  state.lifeFlashTimer = 0.0f;

  if (!state.hasSpawnpoint && worldStack.currentWorld()) {
    state.hasSpawnpoint = true;
    state.spawnpointPosition = spawnPosition;
    state.spawnpointUniverseSeed = worldStack.currentWorld()->seed;
    state.spawnpointBiomeSelection = worldStack.currentWorld()->biomeSelection;
  }

  return true;
}

void ensureDeathScreenHud(DeathSequenceState& deathState) {
  if (deathState.messageText) {
    return;
  }

  auto* text = new hudText(
      "",
      makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -24.0f)),
      glm::vec2(1.15f),
      28,
      HUD::getDefaultFont());
  text->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
  text->setOpacity(0.0f);
  HUD::add(text, RuntimeHudIds::kDeathScreen);
  deathState.messageText = text;
}

void startDeathSequence(DeathSequenceState& deathState, Player& player,
                        WorldStack& worldStack, GameAudioController& audioController,
                        hudPortalInfo* portalInfo, hudPortalTracker* portalTracker,
                        GameChat& gameChat, RuntimeUI::RuntimeUiState& uiState,
                        WorldSaveService::WorldSession& worldSession) {
  if (deathState.active) {
    return;
  }

  ensureDeathScreenHud(deathState);

  deathState.messages = &motivationalMessages();
  deathState.messageIndex = randomMotivationalMessageIndex(*deathState.messages);
  deathState.paused = false;
  deathState.pausedRenderTimeSeconds = 0.0;

  deathState.active = true;
  deathState.elapsedSeconds = 0.0f;
  updateDeathScreenMessage(deathState, player);

  player.tryCloseInventory();
  if (portalInfo) {
    portalInfo->cancelEditing();
  }
  if (portalTracker) {
    portalTracker->closeMenu();
  }
  gameChat.close();

  uiState.settingsMenuOpen = false;
  uiState.settingsDiscardConfirmOpen = false;
  uiState.returnToLauncherRequested = false;
  uiState.controlsCaptureOpen = false;
  uiState.controlsCaptureIgnoreMouseLeft = false;
  uiState.controlsCaptureActionId.clear();
  uiState.controlsWarningMessage.clear();

  Input::setFocusMode(Input::FocusMode::GAMEPLAY);
  Input::setCursorVisible(false);
  HUD::setVisible(false);
  HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, true);

  Player::PersistentState deathStateData = player.capturePersistentState();
  if (!applySpawnWaitingState(deathStateData, player, worldStack, false)) {
    RuntimeAppInternal::printBootstrapError(
        "Failed to move the player to the spawnpoint during death.");
    deathStateData.lifePoints = 0;
  }

  player.applyPersistentState(deathStateData);
  player.setDeathSequenceState(true, deathState.elapsedSeconds, deathState.message);
  player.setAudioController(&audioController);
  ++worldSession.deathCount;

  audioController.onDeathSequenceStarted();
}

bool finalizeDeathSequence(WorldSaveService::WorldSession& worldSession, Player& player,
                           WorldStack& worldStack, GameAudioController& audioController,
                           GameChat& gameChat, hudPortalTracker* portalTracker,
                           RuntimeUI::RuntimeUiState& uiState,
                           double totalPlaytimeSeconds,
                           std::string* outError) {
  (void)audioController;

  Player::PersistentState respawnState = player.capturePersistentState();
  if (!applySpawnWaitingState(respawnState, player, worldStack, true, outError)) {
    return false;
  }

  player.applyPersistentState(respawnState);

  gameChat.close();
  uiState.settingsMenuOpen = false;
  uiState.settingsDiscardConfirmOpen = false;
  uiState.controlsCaptureOpen = false;
  uiState.controlsCaptureIgnoreMouseLeft = false;
  uiState.controlsCaptureActionId.clear();
  uiState.controlsWarningMessage.clear();
  uiState.saveToastTimer = 0.0f;

  HUD::setVisible(true);
  HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, false);

  if (portalTracker) {
    portalTracker->closeMenu();
  }

  std::string saveError;
  if (!saveCurrentWorldSession(worldSession, player, worldStack, portalTracker,
                               uiState, totalPlaytimeSeconds, false, nullptr,
                               &saveError)) {
    if (outError) {
      *outError = saveError;
    }
    return false;
  }

  return true;
}

void updateGame(Player& player, WorldStack& worldStack,
                GameAudioController& audioController, hudPortalInfo* portalInfo,
                hudPortalTracker* portalTracker,
                const GameChat& gameChat, bool deathSequenceActive,
                bool deathSequencePaused, float dt) {
  if (ENGINE::ISPAUSED() || deathSequencePaused) {
    return;
  }

  PlayerUpdateMode playerUpdateMode = PlayerUpdateMode::FullGameplay;
  if (deathSequenceActive || Input::hasUiFocus() || (portalInfo && portalInfo->isEditing()) ||
      (portalTracker && portalTracker->isMenuOpen())) {
    playerUpdateMode = PlayerUpdateMode::Frozen;
  } else if (player.isInventoryOpen() || gameChat.isOpen()) {
    playerUpdateMode = PlayerUpdateMode::SimulationOnly;
  }

  player.update(dt, worldStack, playerUpdateMode);
  worldStack.update(player.camera.position, player.camera.getForward(), dt);
  worldStack.updateEnemies(player, audioController, dt);
  worldStack.updateDroppedItems(player.camera.position, dt,
                                [&player, &audioController](const InventoryItem& pickedItem) {
                                  if (!player.tryAddItemToInventory(pickedItem, 1)) {
                                    return false;
                                  }
                                  audioController.onItemCollected();
                                  return true;
                                });
}

void renderFrame(GLFWwindow* window, Renderer& renderer, WorldStack& worldStack,
                 Player& player, float currentTime,
                 const RuntimeAppInternal::RuntimeDebugFlags& debugFlags,
                 const DeathSequenceState* deathState = nullptr) {
  ENGINE::BEGINPERFFRAME();

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (width <= 0 || height <= 0) {
    return;
  }

  glViewport(0, 0, width, height);
  const float aspect = static_cast<float>(width) / static_cast<float>(height);
  ENGINE::BEGINGPUFRAMEQUERY();
  renderer.render(worldStack, player, aspect, currentTime, debugFlags.wireframeMode,
                  debugFlags.debugThirdPersonView);
  if (deathState && deathState->active) {
    renderer.renderDeathScreenBackground(glm::ivec2(width, height), currentTime,
                                         deathScreenVignetteExtra(
                                             deathState->elapsedSeconds));
  }
  HUD::update(width, height);
  HUD::render(width, height);
#if defined(VP_ENABLE_DEV_TOOLS)
  if (RuntimeUI::shouldRenderDeveloperUi()) {
    ImGuiLayer::beginFrame();
    if (Input::hasUiFocus()) {
      BiomeTeleportWindow::syncCurrentBiome(worldStack.currentBiomeSelection());
      BiomeTeleportWindow::draw();
    }
    RuntimeUI::drawDeveloperUi(worldStack, player);
    ImGuiLayer::render();
  }
#endif
  ENGINE::ENDGPUFRAMEQUERY();
}

ENGINE::AUDIO::AudioListenerState buildListenerState(const Player& player) {
  ENGINE::AUDIO::AudioListenerState state;
  state.position = player.camera.position;
  state.forward = player.camera.getForward();
  state.up = player.camera.getUp();
  state.velocity = player.velocity;
  return state;
}

GameAudioFrameState buildAudioFrameState(const RuntimeUI::RuntimeUiState& uiState,
                                         const Player& player,
                                         const WorldStack& worldStack,
                                         const DeathSequenceState& deathState) {
  GameAudioFrameState state;
  state.paused = ENGINE::ISPAUSED() || deathState.paused;
  state.settingsMenuOpen = uiState.settingsMenuOpen;
  state.inventoryOpen = player.isInventoryOpen();
  state.portalPreviewActive =
      player.nestedPreview.active && player.nestedPreview.fade > 0.0f;
  state.deathScreenActive = deathState.active;
  state.deathScreenFadeOutActive =
      deathState.active && deathScreenMusicFadeOutActive(deathState.elapsedSeconds);
  state.biomePresetId = worldStack.currentBiomeSelection().presetId;
  return state;
}

#if defined(VP_ENABLE_DEV_TOOLS)
void handleDeveloperTools(WorldStack& worldStack, Player& player,
                          hudPortalInfo* portalInfo,
                          const WorldSaveService::WorldSession& worldSession,
                          double currentTime, double lastTime) {
  BiomeSelection targetBiome = worldSession.manifest.rootBiomeSelection;
  if (BiomeTeleportWindow::consumeTeleportRequest(targetBiome)) {
    if (DebugBiomeTools::teleportToBiome(
            worldStack, player, portalInfo, targetBiome, ClientDefaults::kRootSeed,
            ClientDefaults::kPlayerSpawnPosition, currentTime, lastTime)) {
      BiomeTeleportWindow::setSelectedBiome(targetBiome);
    }
  }

  if (BiomeTeleportWindow::consumeUpdateLocationRequest()) {
    DebugBiomeTools::updatePlayerLocationAroundPlayer(worldStack, player);
  }
}
#endif

void rebuildHudIfRequested(Player& player, WorldStack& worldStack, Renderer& renderer,
                           GameAudioController& audioController, GLFWwindow* window,
                           RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle,
                           GameChat& gameChat, DeathSequenceState& deathState,
                           hudPortalInfo*& portalInfo,
                           hudPortalTracker*& portalTracker) {
  if (!settingsBundle.uiState.hudRebuildRequested) {
    return;
  }

  WorldSaveService::PlayerData::PortalTrackerState trackerState{};
  bool hasTrackerState = false;
  if (portalTracker) {
    trackerState = portalTracker->capturePersistentState();
    hasTrackerState = trackerState.trackingActive;
  }

  settingsBundle.uiState.hudRebuildRequested = false;
  deathState.messageText = nullptr;
  HUD::clear();
  portalInfo = RuntimeUI::setupHUD(
      player, worldStack, renderer, audioController, window, settingsBundle.applied,
      settingsBundle.pending, settingsBundle.availableFonts,
      settingsBundle.availableResolutions, settingsBundle.uiState, &portalTracker);
  gameChat.setupHud();
  gameChat.syncHudState();
  if (portalTracker && hasTrackerState) {
    portalTracker->applyPersistentState(trackerState);
  }
  RuntimeUI::syncCursorVisibility(player, portalTracker);

  if (!deathState.active) {
    return;
  }

  ensureDeathScreenHud(deathState);
  updateDeathScreenMessage(deathState, player);
  HUD::setVisible(false);
  HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, true);
}

void logShutdownStep(const char* message) {
  RuntimeAppInternal::printBootstrapInfo(message);
}

}  // namespace

namespace RuntimeAppInternal {

RuntimeLoopExitReason runMainLoop(GLFWwindow* window, Renderer& renderer,
                                  WorldStack& worldStack, Player& player,
                                  GameAudioController& audioController,
                                  GameChat& gameChat,
                                  WorldSaveService::WorldSession& worldSession,
                                  hudPortalInfo*& portalInfo,
                                  hudPortalTracker*& portalTracker,
                                  RuntimeSettingsBundle& settingsBundle) {
  // --- 1. Engine Timer Bootstrap ---
  RuntimeDebugFlags debugFlags;
  double lastTime = glfwGetTime();
  if (!ENGINE::ISINITIALIZED()) {
    printBootstrapInfo("Starting engine timers...");
    ENGINE::INIT(lastTime);
    printBootstrapSuccess("Bootstrap complete!");
  }

  double totalPlaytimeSeconds = worldSession.totalPlaytimeSeconds;
  double lastAutosavePlaytimeSeconds = totalPlaytimeSeconds;
  std::string autosaveError;
  const std::uint64_t pauseListenerId = ENGINE::ADDPAUSELISTENER(
      [&worldSession, &player, &worldStack, &settingsBundle,
       &totalPlaytimeSeconds, &lastAutosavePlaytimeSeconds, &autosaveError,
       portalTracker](
          bool paused) {
        if (!paused) {
          return;
        }

        if (!saveCurrentWorldSession(worldSession, player, worldStack,
                                     portalTracker, settingsBundle.uiState,
                                     totalPlaytimeSeconds, true, nullptr,
                                     &autosaveError)) {
          if (!autosaveError.empty()) {
            RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
          }
          return;
        }

        lastAutosavePlaytimeSeconds = totalPlaytimeSeconds;
      });

  DeathSequenceState deathState;
  ensureDeathScreenHud(deathState);
  HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, false);

  // --- 2. Main Runtime Loop ---
  while (!glfwWindowShouldClose(window)) {
    // --- 2.1 Frame Timing & Input ---
    const double currentTime = glfwGetTime();
    // rawDt drives frame metrics/FPS; simDt is clamped to keep gameplay stable after hitches.
    const float rawDt = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    ENGINE::UPDATE(currentTime, rawDt);
    const float simDt = glm::min(ENGINE::GETDELTATIME(), 0.05f);
    const auto cpuFrameStart = std::chrono::steady_clock::now();
    Input::update();
    auto& inputActions = InputMapping::InputActionSystem::instance();
    const bool deathSequenceActive = deathState.active;
    const bool deathSequencePaused = deathState.paused;
    inputActions.setCaptureMode(
        !deathSequenceActive && settingsBundle.uiState.controlsCaptureOpen);
    if (deathSequenceActive || settingsBundle.uiState.controlsCaptureOpen || gameChat.isOpen() ||
        (portalInfo && portalInfo->isEditing())) {
      inputActions.clearActiveContexts();
    } else if (ENGINE::ISPAUSED() || player.isInventoryOpen() ||
               (portalTracker && portalTracker->isMenuOpen()) ||
               Input::hasUiFocus()) {
      inputActions.setActiveContexts(
          {InputMapping::InputContext::UiNavigation,
           InputMapping::InputContext::Ui,
           InputMapping::InputContext::Gameplay});
    } else {
      inputActions.setActiveContexts(
          {InputMapping::InputContext::Ui,
           InputMapping::InputContext::Gameplay});
    }

    if (!ENGINE::ISPAUSED() && !deathSequencePaused) {
      totalPlaytimeSeconds += static_cast<double>(simDt);
    }

    // --- 2.2 UI Commands & Global Shortcuts ---
    const bool allowOpenChat =
        !deathSequenceActive &&
        !ENGINE::ISPAUSED() && player.transition == PlayerTransition::NONE &&
        !player.isInventoryOpen() && (!portalInfo || !portalInfo->isEditing()) &&
        (!portalTracker || !portalTracker->isMenuOpen()) &&
        !Input::hasUiFocus();
    GameChatCommandContext chatCommandContext{
        player,
        worldStack,
        debugFlags.wireframeMode,
        debugFlags.debugThirdPersonView,
    };

    const bool chatConsumedInput =
        deathSequenceActive
            ? false
            : gameChat.handleFrameInput(chatCommandContext, allowOpenChat);
    if (!deathSequenceActive && !chatConsumedInput) {
      RuntimeUI::handleGlobalShortcuts(
          portalInfo, portalTracker, worldStack, player, audioController,
          settingsBundle.uiState,
          settingsBundle.applied, settingsBundle.pending);
    }

    gameChat.syncHudState();
    RuntimeUI::syncHudMenuState(settingsBundle.uiState);
    RuntimeUI::syncDebugHudState(settingsBundle.uiState, settingsBundle.applied);
    RuntimeUI::updateSaveToast(settingsBundle.uiState, rawDt);
    RuntimeUI::syncSaveToastState(settingsBundle.uiState);
    RuntimeUI::syncCursorVisibility(player, portalTracker);
    if (deathSequenceActive) {
      Input::setCursorVisible(false);
    }

    // --- 2.3 Gameplay & Audio ---
    updateGame(player, worldStack, audioController, portalInfo, portalTracker,
               gameChat, deathSequenceActive, deathSequencePaused, simDt);

    if (!deathState.active && !player.isAlive()) {
      startDeathSequence(deathState, player, worldStack, audioController, portalInfo,
                         portalTracker, gameChat, settingsBundle.uiState, worldSession);
    }

    if (deathState.active && deathState.messageText) {
      deathState.elapsedSeconds =
          deathState.paused
              ? deathState.elapsedSeconds
              : glm::min(DeathSequenceState::kDurationSeconds,
                         deathState.elapsedSeconds + rawDt);
      player.setDeathSequenceState(true, deathState.elapsedSeconds, deathState.message);
      deathState.messageText->setOpacity(
          deathScreenTextOpacity(deathState.elapsedSeconds));
    }

    audioController.syncFrame(buildListenerState(player),
                              buildAudioFrameState(settingsBundle.uiState, player, worldStack,
                                                  deathState),
                              rawDt);

    // --- 2.4 Rendering & Capture ---
    renderFrame(
        window,
        renderer,
        worldStack,
        player,
        deathSequencePaused ? static_cast<float>(deathState.pausedRenderTimeSeconds)
                            : static_cast<float>(ENGINE::GETTIME()),
        debugFlags,
        &deathState
    );

    if (settingsBundle.uiState.returnToLauncherRequested) {
      break;
    }

    if (InputMapping::InputActionSystem::instance().wasPressed(
            InputActionIds::kTakeScreenshot) &&
        canCaptureGameplayScreenshot(player, gameChat, portalInfo, portalTracker)) {
      if (!captureGameplayScreenshot(window)) {
        std::printf("[Screenshot] Failed to capture screenshot.\n");
      }
    }

    if (!deathSequenceActive && !ENGINE::ISPAUSED() &&
        totalPlaytimeSeconds - lastAutosavePlaytimeSeconds >= 300.0) {
      autosaveError.clear();
      if (saveCurrentWorldSession(worldSession, player, worldStack, portalTracker,
                                  settingsBundle.uiState, totalPlaytimeSeconds,
                                  true, nullptr, &autosaveError)) {
        lastAutosavePlaytimeSeconds = totalPlaytimeSeconds;
      } else if (!autosaveError.empty()) {
        RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
      }
    }

    // --- 2.5 Developer Tools & Frame Metrics ---
#if defined(VP_ENABLE_DEV_TOOLS)
    handleDeveloperTools(worldStack, player, portalInfo, worldSession, currentTime,
                         lastTime);
#endif

    const auto cpuFrameEnd = std::chrono::steady_clock::now();
    const float cpuFrameMs =
        std::chrono::duration<float, std::milli>(cpuFrameEnd - cpuFrameStart).count();
    const float fpsInstant = rawDt > 0.0f ? (1.0f / rawDt) : 0.0f;
    ENGINE::ENDPERFFRAME(cpuFrameMs, fpsInstant);

    // --- 2.6 Present & Deferred HUD Work ---
    glfwSwapBuffers(window);

    if (deathState.active &&
        deathState.elapsedSeconds >= DeathSequenceState::kDurationSeconds) {
      std::string respawnError;
      if (!finalizeDeathSequence(worldSession, player, worldStack, audioController,
                                 gameChat, portalTracker, settingsBundle.uiState,
                                 totalPlaytimeSeconds, &respawnError)) {
        if (!respawnError.empty()) {
          RuntimeAppInternal::printBootstrapError(respawnError.c_str());
        }
        settingsBundle.uiState.returnToLauncherRequested = true;
        break;
      }

      deathState.active = false;
      deathState.paused = false;
      deathState.pausedRenderTimeSeconds = 0.0;
      deathState.elapsedSeconds = 0.0f;
      deathState.message.clear();
      deathState.messages = nullptr;
      deathState.messageIndex = 0;
      if (deathState.messageText) {
        deathState.messageText->setText("");
        deathState.messageText->setOpacity(0.0f);
      }
      player.setDeathSequenceState(false, 0.0f);
      lastAutosavePlaytimeSeconds = totalPlaytimeSeconds;
    }

    rebuildHudIfRequested(player, worldStack, renderer, audioController, window,
                          settingsBundle, gameChat, deathState, portalInfo,
                          portalTracker);
  }

  autosaveError.clear();
  if (!saveCurrentWorldSession(worldSession, player, worldStack, portalTracker,
                               settingsBundle.uiState, totalPlaytimeSeconds, false,
                               nullptr, &autosaveError) &&
      !autosaveError.empty()) {
    RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
  }

  ENGINE::REMPAUSELISTENER(pauseListenerId);
  const bool returnToLauncher =
      settingsBundle.uiState.returnToLauncherRequested && !glfwWindowShouldClose(window);
  settingsBundle.uiState.returnToLauncherRequested = false;
  ENGINE::SETPAUSED(false);
  return returnToLauncher ? RuntimeLoopExitReason::ReturnToLauncher
                          : RuntimeLoopExitReason::QuitGame;
}

void shutdownGame(GLFWwindow*& window, Renderer& renderer, WorldStack* worldStack) {
  RuntimeAppInternal::printBootstrapInfo("Shutting down game runtime...");

  // --- 1. Developer UI Shutdown ---
#if defined(VP_ENABLE_DEV_TOOLS)
  logShutdownStep("Shutdown Step 1/7 - Developer UI");
  BiomeTeleportWindow::shutdown();
#endif
  logShutdownStep("Shutdown Step 2/7 - ImGui Layer");
  ImGuiLayer::shutdown();

  // --- 2. Gameplay Subsystems ---
  logShutdownStep("Shutdown Step 3/7 - HUD");
  HUD::cleanup();
  if (worldStack) {
    logShutdownStep("Shutdown Step 4/7 - World Stack");
    worldStack->shutdown();
  }
  logShutdownStep("Shutdown Step 5/7 - Renderer");
  renderer.cleanup();
  logShutdownStep("Shutdown Step 6/7 - Biome Registry");
  BiomeRegistry::instance().clear();
  logShutdownStep("Shutdown Step 7/7 - Input");
  Input::shutdown();
  InputMapping::InputActionSystem::instance().shutdown();

  // --- 3. Engine & Window Shutdown ---
  RuntimeAppInternal::printBootstrapInfo("Shutdown Step 8/8 - Engine and window");
  ENGINE::SHUTDOWN();
  Bootstrap::shutdownWindow(window);
  window = nullptr;
  RuntimeAppInternal::printBootstrapSuccess("Game runtime shutdown complete.");
}

[[noreturn]] void terminateRuntimeProcess(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
}

}  // namespace RuntimeAppInternal
