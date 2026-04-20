// runtime_app.cpp
// Unity mental model: this is the top-level game bootstrap script.
// Keep this file high level. Detailed bootstrap and frame logic live in the
// split runtime_app_bootstrap.cpp and runtime_app_loop.cpp files.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

// Third-party
#include <GLFW/glfw3.h>

// Internal - Config & Base
#include "client_defaults.hpp"
#include "engine/bootstrap.hpp"
#include "engine/engine.hpp"

// Internal - Runtime & Logic
#include "runtime/app/runtime_app.hpp"
#include "runtime/app/runtime_app_internal.hpp"
#include "runtime/state/game_chat.hpp"
#include "runtime/launcher/world_launcher.hpp"
#include "player/player.hpp"

// Internal - World & Audio
#include "world/biome/biome_registry.hpp"
#include "world/persistence/world_save_service.hpp"
#include "world/persistence/world_stack.hpp"
#include "audio/game_audio_controller.hpp"

// Internal - Rendering & UI
#include "render/core/renderer.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "ui/biome_teleport_window.hpp"
#include "ui/imgui_layer.hpp"

namespace {

    void printStartupBanner() {
        std::printf("-------------------------------------------------------------");
        std::printf("\n\nThe fractal world is nothing more than a poorly told dream...\n\n");
        std::printf("\n- With love, Saitox.\n\n");
        std::printf("-------------------------------------------------------------\n\n\n\n");
        std::fflush(stdout);
    }

    Player preparePlayer(const GameSettings& settings,
                         const glm::vec3& resolvedSpawnPosition,
                         const WorldSaveService::WorldSession& worldSession) {
        RuntimeAppInternal::printBootstrapInfo("Preparing the player...");

        Player player;
        player.camera.position = resolvedSpawnPosition;
        player.camera.sensitivity = settings.mouseSensitivity;
        player.normalFov = settings.fieldOfView;
        player.camera.baseFov = settings.fieldOfView;
        if (worldSession.hasPlayerData) {
            player.applyPersistentState(worldSession.playerData.playerState);
            player.camera.sensitivity = settings.mouseSensitivity;
            player.normalFov = settings.fieldOfView;
            player.camera.baseFov = settings.fieldOfView;
        }
        if (!player.hasSpawnpoint()) {
            player.setSpawnpoint(resolvedSpawnPosition,
                                 worldSession.startUniverseSeed,
                                 worldSession.startUniverseBiomeSelection,
                                 worldSession.playerData.traversalStack);
        }

        char spawnBuffer[64];
        std::snprintf(spawnBuffer, sizeof(spawnBuffer), "%.2f, %.2f, %.2f",
            player.camera.position.x, player.camera.position.y,
            player.camera.position.z);

        RuntimeAppInternal::printBootstrapDetail("Player Spawn Pos:", spawnBuffer);
        RuntimeAppInternal::printBootstrapSuccess("Player initialized!");

        return player;
    }

    void initializeDeveloperUi(GLFWwindow* window, const BiomeSelection& rootBiomeSelection) {
#if defined(VP_ENABLE_DEV_TOOLS)
        (void)window;
        BiomeTeleportWindow::setAvailableBiomes(BiomeRegistry::instance().buildSelectableBiomes());
        BiomeTeleportWindow::setSelectedBiome(rootBiomeSelection);
#else
        (void)window;
        (void)rootBiomeSelection;
#endif
    }

    void resetUiStateForLauncher(RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle) {
        settingsBundle.uiState.settingsMenuOpen = false;
        settingsBundle.uiState.settingsDiscardConfirmOpen = false;
        settingsBundle.uiState.hudRebuildRequested = false;
        settingsBundle.uiState.returnToLauncherRequested = false;
        settingsBundle.uiState.saveToastTimer = 0.0f;
    }

    void cleanupGameplaySession(WorldStack& worldStack,
                                RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle) {
        ENGINE::SETPAUSED(false);
        HUD::setVisible(true);
        HUD::clear();
        worldStack.shutdown();
        resetUiStateForLauncher(settingsBundle);
    }

} // namespace

namespace VoxelParadox {

    int runRuntimeApp() {
        // --- 1. Basic Initialization ---
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        printStartupBanner();

        std::string settingsLoadError;
        RuntimeAppInternal::RuntimeSettingsBundle settingsBundle =
            RuntimeAppInternal::loadRuntimeSettings(&settingsLoadError);

        if (!settingsLoadError.empty()) {
            RuntimeAppInternal::printBootstrapError(settingsLoadError.c_str());
        }

        // --- 2. Biome & Resource Loading ---
        BiomeSelection rootBiomeSelection;
        std::shared_ptr<const VoxelGame::BiomePreset> rootBiomePreset;

        if (!RuntimeAppInternal::loadRequiredBiomePresets(rootBiomeSelection, rootBiomePreset)) {
            return -1;
        }
        (void)rootBiomePreset;

        // --- 3. Window, Graphics Context, and UI Bootstrap ---
        Renderer renderer;
        GLFWwindow* window = nullptr;
        const Bootstrap::Config bootstrapConfig =
            RuntimeAppInternal::makeBootstrapConfig(settingsBundle.applied);

        if (!Bootstrap::initialize(bootstrapConfig, window)) {
            return -1;
        }

        if (!renderer.init()) {
            Bootstrap::shutdownWindow(window);
            return -1;
        }
        renderer.setRenderScale(settingsBundle.applied.renderScale);
        renderer.setAntiAliasingSamples(settingsBundle.applied.antiAliasingSamples);
        renderer.setAdvancedLightingEnabled(settingsBundle.applied.advancedLightingEnabled);
        renderer.setCloudsEnabled(settingsBundle.applied.cloudsEnabled);
        renderer.setCloudQuality(settingsBundle.applied.cloudQuality);

        const bool imguiInitialized = ImGuiLayer::initialize(window);
        Bootstrap::reportImGuiStatus(imguiInitialized);
        if (!imguiInitialized) {
            RuntimeAppInternal::shutdownGame(window, renderer, nullptr);
            return -1;
        }

        ENGINE::INIT(glfwGetTime());
        if (!HUD::init()) {
            RuntimeAppInternal::printBootstrapError("Failed to initialize the HUD.");
            RuntimeAppInternal::shutdownGame(window, renderer, nullptr);
            return -1;
        }

        // --- 4. Launcher <-> Gameplay Session Loop ---
        while (!glfwWindowShouldClose(window)) {
            resetUiStateForLauncher(settingsBundle);
            HUD::setDefaultFont(settingsBundle.applied.fontAssetPath());

            WorldSaveService::WorldSession worldSession;
            std::string launcherError;
            const WorldLauncher::RunResult launcherResult =
                WorldLauncher::run(window, rootBiomeSelection, worldSession,
                                   &launcherError);
            if (launcherResult == WorldLauncher::RunResult::ExitGame) {
                break;
            }
            if (launcherResult == WorldLauncher::RunResult::Error) {
                if (!launcherError.empty()) {
                    RuntimeAppInternal::printBootstrapError(launcherError.c_str());
                }
                RuntimeAppInternal::shutdownGame(window, renderer, nullptr);
                return -1;
            }

            try {
                initializeDeveloperUi(window, worldSession.manifest.rootBiomeSelection);
            } catch (const std::exception&) {
                RuntimeAppInternal::shutdownGame(window, renderer, nullptr);
                return -1;
            }

            // --- 5. World & Player Setup ---
            WorldStack worldStack;
            glm::vec3 resolvedSpawnPosition = ClientDefaults::kPlayerSpawnPosition;
            if (!RuntimeAppInternal::prepareWorldFromSession(
                    worldStack, worldSession, ClientDefaults::kPlayerSpawnPosition,
                    Player::kDefaultPlayerRadius, Player::kDefaultStandingHeight,
                    Player::kDefaultStandingEyeHeight, settingsBundle.applied.renderDistance,
                    resolvedSpawnPosition)) {
                RuntimeAppInternal::shutdownGame(window, renderer, &worldStack);
                return -1;
            }

            Player player = preparePlayer(settingsBundle.applied, resolvedSpawnPosition,
                                          worldSession);

            if (!worldSession.hasPlayerData ||
                !worldSession.playerData.playerState.hasSpawnpoint) {
                worldSession.playerData.hasPlayerState = true;
                worldSession.playerData.playerState = player.capturePersistentState();
                worldSession.playerData.currentUniverseSeed =
                    worldStack.currentWorld() ? worldStack.currentWorld()->seed
                                              : worldSession.startUniverseSeed;
                worldSession.playerData.currentUniverseBiomeSelection =
                    worldStack.currentWorld() ? worldStack.currentWorld()->biomeSelection
                                              : worldSession.startUniverseBiomeSelection;

                std::string spawnpointInitError;
                if (!WorldSaveService::savePlayerData(worldSession.paths,
                                                      worldSession.playerData,
                                                      &spawnpointInitError)) {
                    if (!spawnpointInitError.empty()) {
                        RuntimeAppInternal::printBootstrapError(
                            spawnpointInitError.c_str());
                    }
                } else {
                    worldSession.hasPlayerData = true;
                }
            }

            // --- 6. Audio Subsystem ---
            RuntimeAppInternal::printBootstrapInfo("Preparing the audio subsystem...");

            ENGINE::AUDIO::AudioManager audioManager;
            std::string audioStatusMessage;
            audioManager.initialize({}, &audioStatusMessage);

            GameAudioController audioController(audioManager);
            audioController.applySettings(settingsBundle.applied.audioSettings);
            player.setAudioController(&audioController);

            RuntimeAppInternal::printBootstrapDetail("Audio Backend:", audioManager.backendReady() ? "OpenAL" : "Silent");
            if (!audioStatusMessage.empty()) {
                RuntimeAppInternal::printBootstrapDetail("Audio Status:", audioStatusMessage);
            }
            RuntimeAppInternal::printBootstrapSuccess("Audio initialized!");

            // --- 7. HUD & Social Systems ---
            RuntimeAppInternal::printBootstrapInfo("Preparing the HUD...");
            HUD::clear();

            GameChat gameChat;
            hudPortalTracker* portalTracker = nullptr;
            hudPortalInfo* portalInfo = RuntimeUI::setupHUD(
                player, worldStack, renderer, audioController, window,
                settingsBundle.applied, settingsBundle.pending,
                settingsBundle.availableFonts, settingsBundle.availableResolutions,
                settingsBundle.uiState, &portalTracker
            );

            gameChat.setupHud();
            gameChat.syncHudState();
            if (portalTracker && worldSession.playerData.hasPortalTrackerState) {
                portalTracker->applyPersistentState(
                    worldSession.playerData.portalTrackerState);
            }
            RuntimeAppInternal::printBootstrapSuccess("HUD initialized!");

            // --- 8. Main Loop ---
            const RuntimeAppInternal::RuntimeLoopExitReason loopExit =
                RuntimeAppInternal::runMainLoop(
                    window, renderer, worldStack, player,
                    audioController, gameChat, worldSession, portalInfo, portalTracker,
                    settingsBundle
                );

            cleanupGameplaySession(worldStack, settingsBundle);
            if (loopExit == RuntimeAppInternal::RuntimeLoopExitReason::ReturnToLauncher) {
                continue;
            }

            break;
        }

        RuntimeAppInternal::printBootstrapInfo("Game exit requested. Beginning shutdown...");
        RuntimeAppInternal::shutdownGame(window, renderer, nullptr);
        RuntimeAppInternal::terminateRuntimeProcess(0);

        return 0; // Adicionado retorno padrÃ£o de sucesso
    }

} // namespace VoxelParadox
