// runtime_app_loop.cpp
// Unity mental model: Per-frame runtime flow.
// Coordinates input, gameplay, audio, rendering, and shutdown.

#pragma region Includes

// 1. Standard Library
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// 3. Internal Engine/Core Modules
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

// 4. Local Project Modules
#include "runtime/app/runtime_app_internal.hpp"
#include "runtime/app/runtime_app_death_sequence.hpp"
#include "runtime/app/runtime_app_loop_shared.hpp"
#include "runtime/app/runtime_app_screenshot.hpp"
#include "audio/game_audio_controller.hpp"
#include "debug/biome_debug_tools.hpp"
#include "gameplay/gameplay_status.hpp"
#include "player/player.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "render/hud/hud_text.hpp"
#include "render/core/renderer.hpp"
#include "runtime/state/game_chat.hpp"
#include "runtime/ui/runtime_hud_ids.hpp"
#include "runtime/ui/runtime_ui.hpp"
#include "ui/biome_teleport_window.hpp"
#include "ui/imgui_layer.hpp"
#include "world/biome/biome_registry.hpp"
#include "world/persistence/world_save_service.hpp"
#include "world/persistence/world_stack.hpp"

#pragma endregion

namespace {

    using RuntimeAppInternal::DeathSequenceState;
    using RuntimeAppInternal::deathScreenMusicFadeOutActive;
    using RuntimeAppInternal::deathScreenVignetteExtra;
    using RuntimeAppInternal::ensureDeathScreenHud;
    using RuntimeAppInternal::updateDeathScreenMessage;

#pragma region 1. Core Frame Routines
    // --- 1. Core Frame Routines ---

    void updateGame(Player& player, WorldStack& worldStack,
        GameAudioController& audioController, hudPortalInfo* portalInfo,
        hudPortalTracker* portalTracker, GameChat& gameChat,
        bool deathSequenceActive, bool deathSequencePaused, float dt) {

        if (ENGINE::ISPAUSED() || deathSequencePaused) {
            return;
        }

        PlayerUpdateMode playerUpdateMode = PlayerUpdateMode::FullGameplay;

        if (deathSequenceActive || Input::hasUiFocus() || (portalInfo && portalInfo->isEditing()) ||
            (portalTracker && portalTracker->isMenuOpen())) {
            playerUpdateMode = PlayerUpdateMode::Frozen;
        }
        else if (player.isInventoryOpen() || gameChat.isOpen()) {
            playerUpdateMode = PlayerUpdateMode::SimulationOnly;
        }

        player.update(dt, worldStack, portalTracker, &gameChat, playerUpdateMode);
        worldStack.update(player.camera.position, player.camera.getForward(), dt);
        worldStack.updateEnemies(player, audioController, dt);

        worldStack.updateDroppedItems(
            player.camera.position, dt,
            [&player, &audioController](const InventoryItem& pickedItem) {
                if (!player.tryAddItemToInventory(pickedItem, 1)) {
                    return false;
                }
                audioController.onItemCollected();
                return true;
            }
        );
    }

    void dispatchPlayerNotifications(Player& player, GameChat& gameChat) {
        const std::vector<Player::NotificationEvent> events =
            player.consumeNotificationEvents();

        for (const Player::NotificationEvent event : events) {
            switch (event) {
            case Player::NotificationEvent::FirstVersalAcquired:
                gameChat.pushFirstVersalNotification();
                break;
            }
        }
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

        renderer.render(
            worldStack, player, aspect, currentTime,
            debugFlags.wireframeMode, debugFlags.debugThirdPersonView
        );

        if (deathState && deathState->active) {
            renderer.renderDeathScreenBackground(
                glm::ivec2(width, height), currentTime,
                deathScreenVignetteExtra(deathState->elapsedSeconds)
            );
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
        state.portalPreviewActive = player.nestedPreview.active && player.nestedPreview.fade > 0.0f;
        state.deathScreenActive = deathState.active;
        state.deathScreenFadeOutActive = deathState.active && deathScreenMusicFadeOutActive(deathState.elapsedSeconds);
        state.biomePresetId = worldStack.currentBiomeSelection().presetId;
        return state;
    }

#pragma endregion

#pragma region 2. Developer Tools & HUD Rebuild
    // --- 2. Developer Tools & HUD Rebuild ---

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
        hudPortalInfo*& portalInfo, hudPortalTracker*& portalTracker) {

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
            settingsBundle.availableResolutions, settingsBundle.uiState, &portalTracker
        );

        gameChat.setupHud();
        gameChat.syncHudState();

        if (portalTracker && hasTrackerState) {
            portalTracker->applyPersistentState(trackerState);
        }

        RuntimeUI::syncCursorVisibility(player, portalTracker, gameChat.isOpen());

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

#pragma endregion

}  // namespace

namespace RuntimeAppInternal {

#pragma region 3. Session Saving
    // --- 3. Session Saving ---

    bool saveCurrentWorldSession(
        WorldSaveService::WorldSession& worldSession,
        Player& player,
        WorldStack& worldStack,
        hudPortalTracker* portalTracker,
        RuntimeUI::RuntimeUiState& uiState,
        const GameplayStatus::PersistentState& gameplayStats,
        bool showToast,
        const Player::PersistentState* overriddenPlayerState,
        std::string* outError
    ) {
        if (portalTracker) {
            worldSession.playerData.hasPortalTrackerState = true;
            worldSession.playerData.portalTrackerState =
                portalTracker->capturePersistentState();
        }
        else {
            worldSession.playerData.hasPortalTrackerState = false;
            worldSession.playerData.portalTrackerState = {};
        }

        const bool saved = overriddenPlayerState
            ? WorldSaveService::saveSessionWithPlayerState(
                worldSession,
                *overriddenPlayerState,
                player.camera.position,
                player.camera.orientation,
                worldStack,
                gameplayStats,
                outError
            )
            : WorldSaveService::saveSession(
                worldSession,
                player,
                worldStack,
                gameplayStats,
                outError
            );

        if (!saved) {
            return false;
        }

        worldSession.gameplayStats = gameplayStats;

        if (showToast) {
            RuntimeUI::triggerSaveToast(uiState);
        }

        return true;
    }

#pragma endregion

#pragma region 4. Main Runtime Loop
    // --- 4. Main Runtime Loop ---

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

        auto& gameplayStatus = GameplayStatus::System::instance();
        gameplayStatus.applyPersistentState(worldSession.gameplayStats);
        gameplayStatus.setPlayerXp(player.getExperiencePoints());
        gameplayStatus.setPlayerLevel(player.getExperienceLevel());

        double lastAutosavePlaytimeSeconds = gameplayStatus.playtimeSeconds();
        std::string autosaveError;

        const std::uint64_t pauseListenerId = ENGINE::ADDPAUSELISTENER(
            [&worldSession, &player, &worldStack, &settingsBundle,
            &gameplayStatus, &lastAutosavePlaytimeSeconds, &autosaveError,
            portalTracker](bool paused) {

                if (!paused) {
                    return;
                }

                if (!saveCurrentWorldSession(worldSession, player, worldStack,
                    portalTracker, settingsBundle.uiState,
                    gameplayStatus.capturePersistentState(),
                    true, nullptr, &autosaveError)) {
                    if (!autosaveError.empty()) {
                        RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
                    }
                    return;
                }

                lastAutosavePlaytimeSeconds = gameplayStatus.playtimeSeconds();
            });

        DeathSequenceState deathState;
        ensureDeathScreenHud(deathState);
        HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, false);

        // --- 2. Main Loop Start ---
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

            inputActions.setCaptureMode(!deathSequenceActive && settingsBundle.uiState.controlsCaptureOpen);

            if (deathSequenceActive || settingsBundle.uiState.controlsCaptureOpen || gameChat.isOpen() ||
                (portalInfo && portalInfo->isEditing())) {
                inputActions.clearActiveContexts();
            }
            else if (ENGINE::ISPAUSED() || player.isInventoryOpen() ||
                (portalTracker && portalTracker->isMenuOpen()) ||
                Input::hasUiFocus()) {
                inputActions.setActiveContexts({
                    InputMapping::InputContext::UiNavigation,
                    InputMapping::InputContext::Ui,
                    InputMapping::InputContext::Gameplay
                    });
            }
            else {
                inputActions.setActiveContexts({
                    InputMapping::InputContext::Ui,
                    InputMapping::InputContext::Gameplay
                    });
            }

            if (!ENGINE::ISPAUSED() && !deathSequencePaused) {
                gameplayStatus.addPlaytimeSeconds(static_cast<double>(simDt));
            }

            // --- 2.2 UI Commands & Global Shortcuts ---
            const bool allowOpenChat =
                !deathSequenceActive &&
                !ENGINE::ISPAUSED() &&
                player.transition == PlayerTransition::NONE &&
                !player.isInventoryOpen() &&
                (!portalInfo || !portalInfo->isEditing()) &&
                (!portalTracker || !portalTracker->isMenuOpen()) &&
                !Input::hasUiFocus();

            GameChatCommandContext chatCommandContext{
                player,
                worldStack,
                debugFlags.wireframeMode,
                debugFlags.debugThirdPersonView,
            };

            const bool chatConsumedInput = deathSequenceActive
                ? false
                : gameChat.handleFrameInput(chatCommandContext, allowOpenChat);

            if (!deathSequenceActive && !chatConsumedInput) {
                RuntimeUI::handleGlobalShortcuts(
                    portalInfo, portalTracker, worldStack, player, audioController,
                    settingsBundle.uiState, settingsBundle.applied, settingsBundle.pending
                );
            }

            gameChat.syncHudState();
            RuntimeUI::syncHudMenuState(settingsBundle.uiState);
            RuntimeUI::syncDebugHudState(settingsBundle.uiState, settingsBundle.applied);
            RuntimeUI::updateSaveToast(settingsBundle.uiState, rawDt);
            RuntimeUI::syncSaveToastState(settingsBundle.uiState);
        RuntimeUI::syncCursorVisibility(player, portalTracker, gameChat.isOpen());

            if (deathSequenceActive) {
                Input::setCursorVisible(false);
            }

            // --- 2.3 Gameplay & Audio ---
            updateGame(player, worldStack, audioController, portalInfo, portalTracker,
                gameChat, deathSequenceActive, deathSequencePaused, simDt);
            dispatchPlayerNotifications(player, gameChat);
            gameChat.syncHudState();

            if (!deathState.active && !player.isAlive()) {
                startDeathSequence(deathState, player, worldStack, audioController, portalInfo,
                    portalTracker, gameChat, settingsBundle.uiState);
            }

            if (deathState.active && deathState.messageText) {
                deathState.elapsedSeconds = deathState.paused
                    ? deathState.elapsedSeconds
                    : glm::min(DeathSequenceState::kDurationSeconds,
                        deathState.elapsedSeconds + rawDt);

                player.setDeathSequenceState(true, deathState.elapsedSeconds, deathState.message);
                deathState.messageText->setOpacity(deathScreenTextOpacity(deathState.elapsedSeconds));
            }

            audioController.syncFrame(
                buildListenerState(player),
                buildAudioFrameState(settingsBundle.uiState, player, worldStack, deathState),
                rawDt
            );

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

            if (InputMapping::InputActionSystem::instance().wasPressed(InputActionIds::kTakeScreenshot) &&
                canCaptureGameplayScreenshot(player, gameChat, portalInfo, portalTracker)) {
                if (!captureGameplayScreenshot(window)) {
                    std::printf("[Screenshot] Failed to capture screenshot.\n");
                }
            }

            if (!deathSequenceActive && !ENGINE::ISPAUSED() &&
                gameplayStatus.playtimeSeconds() - lastAutosavePlaytimeSeconds >= 300.0) {
                autosaveError.clear();
                if (saveCurrentWorldSession(worldSession, player, worldStack, portalTracker,
                    settingsBundle.uiState,
                    gameplayStatus.capturePersistentState(), true,
                    nullptr, &autosaveError)) {
                    lastAutosavePlaytimeSeconds = gameplayStatus.playtimeSeconds();
                }
                else if (!autosaveError.empty()) {
                    RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
                }
            }

            // --- 2.5 Developer Tools & Frame Metrics ---
#if defined(VP_ENABLE_DEV_TOOLS)
            handleDeveloperTools(worldStack, player, portalInfo, worldSession, currentTime, lastTime);
#endif

            const auto cpuFrameEnd = std::chrono::steady_clock::now();
            const float cpuFrameMs = std::chrono::duration<float, std::milli>(cpuFrameEnd - cpuFrameStart).count();
            const float fpsInstant = rawDt > 0.0f ? (1.0f / rawDt) : 0.0f;

            ENGINE::ENDPERFFRAME(cpuFrameMs, fpsInstant);

            // --- 2.6 Present & Deferred HUD Work ---
            glfwSwapBuffers(window);

            if (deathState.active && deathState.elapsedSeconds >= DeathSequenceState::kDurationSeconds) {
                std::string respawnError;
                if (!finalizeDeathSequence(worldSession, player, worldStack, audioController,
                    gameChat, portalTracker, deathState, settingsBundle.uiState,
                    gameplayStatus.capturePersistentState(),
                    &respawnError)) {
                    if (!respawnError.empty()) {
                        RuntimeAppInternal::printBootstrapError(respawnError.c_str());
                    }
                    settingsBundle.uiState.returnToLauncherRequested = true;
                    break;
                }

                resetDeathSequenceState(deathState, player);
                lastAutosavePlaytimeSeconds = gameplayStatus.playtimeSeconds();
            }

        rebuildHudIfRequested(player, worldStack, renderer, audioController, window,
                settingsBundle, gameChat, deathState, portalInfo,
                portalTracker);
        }

        autosaveError.clear();

        if (!saveCurrentWorldSession(worldSession, player, worldStack, portalTracker,
            settingsBundle.uiState,
            gameplayStatus.capturePersistentState(), false,
            nullptr, &autosaveError) &&
            !autosaveError.empty()) {
            RuntimeAppInternal::printBootstrapError(autosaveError.c_str());
        }

        ENGINE::REMPAUSELISTENER(pauseListenerId);

        const bool returnToLauncher = settingsBundle.uiState.returnToLauncherRequested && !glfwWindowShouldClose(window);
        settingsBundle.uiState.returnToLauncherRequested = false;
        ENGINE::SETPAUSED(false);

        return returnToLauncher ? RuntimeLoopExitReason::ReturnToLauncher
            : RuntimeLoopExitReason::QuitGame;
    }

#pragma endregion

#pragma region 5. Shutdown & Termination
    // --- 5. Shutdown & Termination ---

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

#pragma endregion

}  // namespace RuntimeAppInternal
