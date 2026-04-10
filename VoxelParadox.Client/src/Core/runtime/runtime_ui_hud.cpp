// runtime_ui_internal.cpp
// Unity mental model: UI/HUD view builder and synchronizer.
// Constructs the visual tree for debugging, settings, hotbar, and menus.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cstdio>

// 2. Third-party Libraries
// (None explicitly included here, relies on headers)

// 3. Internal Engine/Core Modules
#include "engine/bootstrap.hpp"
#include "input/input_action_system.hpp"

// 4. Local Project Modules
#include "runtime/runtime_ui_internal.hpp"
#include "client_assets.hpp"
#include "client_defaults.hpp"
#include "render/hud/styles/pause_menu_hud_style.hpp"
#include "render/hud/styles/save_toast_hud_style.hpp"
#include "render/hud/styles/settings_dialog_hud_style.hpp"
#include "render/hud/styles/settings_menu_hud_style.hpp"
#include "runtime/runtime_hud_ids.hpp"

#pragma endregion

namespace RuntimeUI::Detail {

#pragma region 1. Detail Helpers
    // --- 1. Detail Helpers ---
    namespace {

        constexpr int kControlsVisibleRows = 9;

        // This file only builds and syncs visible HUD/menu trees.
        HUDLayout makeDebugHUDLayout(int lineIndex) {
            return makeHUDLayout(
                HUDAnchor::TOP_LEFT,
                glm::vec2(ClientDefaults::kDebugHudMargin,
                    10.0f + ClientDefaults::kDebugHudLineHeight * lineIndex)
            );
        }

        HUDLayout makePlayerStatusLayout() {
            return makeHUDLayout(HUDAnchor::BOTTOM_CENTER, glm::vec2(-120.0f, -50.0f));
        }

        HUDLayout makeSaveToastLayout() {
            return makeHUDLayout(
                HUDAnchor::BOTTOM_RIGHT,
                HUDAnchor::BOTTOM_RIGHT,
                glm::vec2(-20.0f, -28.0f)
            );
        }

        const InputMapping::InputCategoryDefinition* selectedControlsCategory(const RuntimeUI::RuntimeUiState& uiState) {
            const auto& categories = InputMapping::InputActionSystem::instance().catalog().categories;
            if (categories.empty()) {
                return nullptr;
            }

            const std::size_t index = std::min(uiState.controlsCategoryIndex, categories.size() - 1);
            return &categories[index];
        }

        const InputMapping::InputActionDefinition* controlsActionForRow(const RuntimeUI::RuntimeUiState& uiState, int rowIndex) {
            const InputMapping::InputCategoryDefinition* category = selectedControlsCategory(uiState);
            if (!category || rowIndex < 0 || rowIndex >= static_cast<int>(category->actionIds.size())) {
                return nullptr;
            }

            return InputMapping::InputActionSystem::instance().findAction(
                category->actionIds[static_cast<std::size_t>(rowIndex)]
            );
        }

        std::string controlsCaptureActionLabel(const RuntimeUI::RuntimeUiState& uiState) {
            if (uiState.controlsCaptureActionId.empty()) {
                return {};
            }

            const InputMapping::InputActionDefinition* action =
                InputMapping::InputActionSystem::instance().findAction(uiState.controlsCaptureActionId);

            return action ? action->label : std::string{};
        }

    }  // namespace
#pragma endregion

#pragma region 2. HUD Groups Initialization
// --- 2. HUD Groups Initialization ---

// Register every HUD/menu group once before building the actual widgets.
    void createHUDGroups() {
        HUD::createGroup(RuntimeHudIds::kDebugInfo, RuntimeHudIds::kDebugLayer, false);
        HUD::createGroup(RuntimeHudIds::kFpsOnly, RuntimeHudIds::kDebugLayer, false);
        HUD::createGroup(RuntimeHudIds::kPlayerStatus, RuntimeHudIds::kPlayerStatusLayer);
        HUD::createGroup(RuntimeHudIds::kHotbar, 0);

        HUD::createGroup(RuntimeHudIds::kInventoryMenu, RuntimeHudIds::kInventoryMenuLayer, true, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kPauseMenu, RuntimeHudIds::kPauseMenuLayer, false, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSettingsMenu, RuntimeHudIds::kSettingsMenuLayer, false, HUDGroupCategory::MENU);

        HUD::createGroup(RuntimeHudIds::kSettingsGeneralTab, RuntimeHudIds::kSettingsTabLayer, false, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSettingsSoundTab, RuntimeHudIds::kSettingsTabLayer, false, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSettingsControlsTab, RuntimeHudIds::kSettingsTabLayer, false, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSettingsConfirmMenu, RuntimeHudIds::kSettingsConfirmLayer, false, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSettingsControlsCaptureMenu, RuntimeHudIds::kSettingsControlsCaptureLayer, false, HUDGroupCategory::MENU);

        HUD::createGroup(RuntimeHudIds::kPortalTrackerWaypoint, RuntimeHudIds::kPortalTrackerWaypointLayer);
        HUD::createGroup(RuntimeHudIds::kPortalTrackerMenu, RuntimeHudIds::kPortalTrackerMenuLayer, true, HUDGroupCategory::MENU);
        HUD::createGroup(RuntimeHudIds::kSaveToast, RuntimeHudIds::kSaveToastLayer);
        HUD::createGroup(RuntimeHudIds::kDeathScreen, RuntimeHudIds::kDeathScreenLayer, false, HUDGroupCategory::MENU);
    }

#pragma endregion

#pragma region 3. Debug HUD
    // --- 3. Debug HUD ---

    // Lightweight FPS-only overlay used when the full debug text is hidden.
    void addFpsOnlyHUD() {
        attachToHUDGroup(
            HUD::watchFloat(
                "FPS",
                [] { return ENGINE::GETFPS(); },
                makeDebugHUDLayout(0),
                glm::vec2(1.0f), 18, 1,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kFpsOnly
        );
    }

    // Full top-left debug HUD with world, frame, and target state.
    void addDebugHUD(WorldStack& worldStack, Player& player) {
        const auto withCurrentWorld = [&worldStack]() -> FractalWorld* {
            return worldStack.currentWorld();
            };

        attachToHUDGroup(
            HUD::watchFloat(
                "FPS",
                [] { return ENGINE::GETFPS(); },
                makeDebugHUDLayout(0),
                glm::vec2(1.0f), 18, 1,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [](std::string& out) {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), "CPU: %.2f ms", ENGINE::GETCPUFRAMETIMEMS());
                    out = buffer;
                },
                makeDebugHUDLayout(1), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [](std::string& out) {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), "GPU: %.2f ms", ENGINE::GETGPUFRAMETIMEMS());
                    out = buffer;
                },
                makeDebugHUDLayout(2), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [](std::string& out) {
                    char buffer[96];
                    std::snprintf(buffer, sizeof(buffer),
                        "Chunk Render: %.3f ms avg (%d chunks)",
                        ENGINE::GETCHUNKRENDERTIMEMS(),
                        ENGINE::GETRENDEREDCHUNKCOUNT());
                    out = buffer;
                },
                makeDebugHUDLayout(3), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [](std::string& out) {
                    const FractalWorld::RuntimePerformanceToggles& toggles = FractalWorld::getRuntimePerformanceToggles();
                    char buffer[96];
                    std::snprintf(buffer, sizeof(buffer), "Meshing: %s",
                        toggles.greedyMeshingEnabled ? "Greedy" : "Naive");
                    out = buffer;
                },
                makeDebugHUDLayout(4), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [withCurrentWorld](std::string& out) {
                    const FractalWorld* world = withCurrentWorld();
                    if (!world) {
                        out = "Frustum Chunks: -";
                        return;
                    }

                    const FractalWorld::RenderDiagnostics& diagnostics = world->getLastRenderDiagnostics();
                    const int frustumVisible = diagnostics.candidateChunkCount - diagnostics.frustumCulledChunkCount;

                    char buffer[96];
                    std::snprintf(buffer, sizeof(buffer),
                        "Frustum Chunks: %d visible | %d culled",
                        std::max(frustumVisible, 0),
                        diagnostics.frustumCulledChunkCount);
                    out = buffer;
                },
                makeDebugHUDLayout(5), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [withCurrentWorld](std::string& out) {
                    const FractalWorld* world = withCurrentWorld();
                    if (!world) {
                        out = "Clusters: -";
                        return;
                    }

                    const FractalWorld::RenderDiagnostics& diagnostics = world->getLastRenderDiagnostics();
                    char buffer[96];
                    std::snprintf(buffer, sizeof(buffer),
                        "Clusters: %d visible | %d occluded",
                        diagnostics.visibleClusterCount,
                        diagnostics.occlusionCulledChunkCount);
                    out = buffer;
                },
                makeDebugHUDLayout(6), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [withCurrentWorld](std::string& out) {
                    const FractalWorld* world = withCurrentWorld();
                    if (!world) {
                        out = "Visible Chunks: -";
                        return;
                    }

                    const FractalWorld::RenderDiagnostics& diagnostics = world->getLastRenderDiagnostics();
                    char buffer[112];
                    std::snprintf(buffer, sizeof(buffer),
                        "Visible Chunks: %d | Vertices: %d",
                        diagnostics.visibleChunkCount,
                        diagnostics.totalSubmittedVertices);
                    out = buffer;
                },
                makeDebugHUDLayout(7), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchInt(
                "Depth",
                [&worldStack] { return worldStack.currentDepth(); },
                makeDebugHUDLayout(8), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchVec3(
                "Pos",
                [&player] { return player.camera.position; },
                makeDebugHUDLayout(9), glm::vec2(1.0f), 18, 1,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [&worldStack](std::string& out) {
                    out = "Render Distance: ";
                    out += worldStack.currentRenderDistanceName();
                },
                makeDebugHUDLayout(10), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [&worldStack](std::string& out) {
                    out = "Biome: ";
                    out += worldStack.currentBiomeName();
                },
                makeDebugHUDLayout(11), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [&player, &worldStack](std::string& out) {
                    out = "Target Block: ";
                    FractalWorld* world = worldStack.currentWorld();
                    if (!world || !player.hasTarget) {
                        out += "-";
                        return;
                    }
                    out += getBlockDisplayName(world->getBlock(player.targetBlock));
                },
                makeDebugHUDLayout(12), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );

        attachToHUDGroup(
            HUD::watchText(
                [&player, &worldStack](std::string& out) {
                    out = "Break Time Left: ";
                    FractalWorld* world = worldStack.currentWorld();
                    if (!world || !player.hasTarget) {
                        out += "-";
                        return;
                    }

                    const BlockId blockType = world->getBlock(player.targetBlock);
                    float remaining = player.getBreakTimeSeconds(blockType);

                    if (player.isBreakingBlock && player.breakingBlock == player.targetBlock) {
                        remaining = glm::max(0.0f, remaining - player.breakingTimer);
                    }

                    char buffer[32];
                    std::snprintf(buffer, sizeof(buffer), "%.2fs", remaining);
                    out += buffer;
                },
                makeDebugHUDLayout(13), glm::vec2(1.0f), 18,
                ClientDefaults::kDebugHudUpdateIntervalSeconds
            ),
            RuntimeHudIds::kDebugInfo
        );
    }

#pragma endregion

#pragma region 4. Player Status HUD
    // --- 4. Player Status HUD ---

    // Top-right player life/status text.
    void addPlayerStatusHUD(Player& player) {
        if (auto* lifeText = attachToHUDGroup(
            HUD::watchText(
                [&player](std::string& out) {
                    char buffer[48];
                    std::snprintf(buffer, sizeof(buffer), "HP: %d / %d",
                        player.getLifePoints(), player.getMaxLifePoints());
                    out = buffer;
                },
                makePlayerStatusLayout(), glm::vec2(1.0f), 30, 0.0f
            ),
            RuntimeHudIds::kPlayerStatus)) {

            lifeText->setVisualBinder(
                [&player](hudWatchText& watchText) {
                    watchText.setColor(player.getLifeTextColor());
                }
            );
            lifeText->setColor(player.getLifeTextColor());
        }
    }

#pragma endregion

#pragma region 5. Portal Tracker & Toast HUD
    // --- 5. Portal Tracker & Toast HUD ---

    void addPortalTrackerHUD(Player& player, WorldStack& worldStack, hudPortalTracker*& outPortalTracker) {
        outPortalTracker = attachToHUDGroup(
            new hudPortalTracker(&player, &worldStack, 18),
            RuntimeHudIds::kPortalTrackerMenu, 0
        );

        if (outPortalTracker) {
            HUD::add(outPortalTracker);
        }
    }

    void addSaveToastHUD(RuntimeUiState& uiState) {
        const glm::vec3 toastColor = HUDStyles::kSaveToast.textColor;

        if (auto* toast = attachToHUDGroup(
            HUD::watchText(
                [&uiState](std::string& out) {
                    out = uiState.saveToastTimer > 0.0f ? uiState.saveToastText : std::string();
                },
                makeSaveToastLayout(), glm::vec2(1.0f), 18, 0.0f
            ),
            RuntimeHudIds::kSaveToast)) {

            toast->setColor(toastColor);
            toast->setVisualBinder(
                [&uiState, toastColor](hudWatchText& watchText) {
                    const float duration = std::max(uiState.saveToastDuration, 0.001f);
                    const float fadeIn = std::max(uiState.saveToastFadeInDuration, 0.001f);
                    const float fadeOut = std::max(uiState.saveToastFadeOutDuration, 0.001f);
                    const float remaining = glm::clamp(uiState.saveToastTimer, 0.0f, duration);
                    const float elapsed = duration - remaining;

                    const float fadeInAlpha = glm::clamp(elapsed / fadeIn, 0.0f, 1.0f);
                    const float fadeOutAlpha = glm::clamp(remaining / fadeOut, 0.0f, 1.0f);

                    watchText.setOpacity(glm::min(fadeInAlpha, fadeOutAlpha));
                    watchText.setColor(toastColor);
                }
            );
        }
    }

#pragma endregion

#pragma region 6. Pause Menu
    // --- 6. Pause Menu ---

    // Pause menu root layer shown while the game is paused and settings are closed.
    void addPauseMenuHUD(GLFWwindow* window, RuntimeUiState& uiState,
        const GameSettings& appliedSettings,
        GameSettings& pendingSettings) {
        (void)window;

        const PauseMenuHUDStyle& style = HUDStyles::kPauseMenu;

        HUD::add(attachToHUDGroup(
            new hudPanel(style.panel.color),
            RuntimeHudIds::kPauseMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Continue",
                makeCenteredMenuButtonPositioner(-80.0f), 32,
                style.continueButton.normalColor,
                style.continueButton.hoverColor,
                [&uiState]() {
                    uiState.settingsMenuOpen = false;
                    ENGINE::SETPAUSED(false);
                }
            ),
            RuntimeHudIds::kPauseMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Settings",
                makeCenteredMenuButtonPositioner(-25.0f), 32,
                style.settingsButton.normalColor,
                style.settingsButton.hoverColor,
                [&uiState, &appliedSettings, &pendingSettings]() {
                    openSettingsMenu(uiState, appliedSettings, pendingSettings);
                }
            ),
            RuntimeHudIds::kPauseMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Exit",
                makeCenteredMenuButtonPositioner(30.0f), 32,
                style.exitButton.normalColor,
                style.exitButton.hoverColor,
                [&uiState]() {
                    uiState.returnToLauncherRequested = true;
                }
            ),
            RuntimeHudIds::kPauseMenu
        ));
    }

#pragma endregion

#pragma region 7. Settings Menu
    // --- 7. Settings Menu ---

    // Main settings screen with General and Sound tabs plus footer actions.
    void addSettingsMenuHUD(Player& player, WorldStack& worldStack, Renderer& renderer,
        GameAudioController& audioController,
        GameSettings& appliedSettings, GameSettings& pendingSettings,
        const std::vector<std::string>& availableFonts,
        const std::vector<glm::ivec2>& availableResolutions,
        RuntimeUiState& uiState) {
        const SettingsMenuHUDStyle& style = HUDStyles::kSettingsMenu;

        const char* generalGroup = RuntimeHudIds::kSettingsGeneralTab;
        const char* soundGroup = RuntimeHudIds::kSettingsSoundTab;
        const char* controlsGroup = RuntimeHudIds::kSettingsControlsTab;

        const float tabsYOffset = -165.0f;
        const float rowLabelColumn = -20.0f;
        const float arrowLeftX = 25.0f;
        const float arrowRightX = 220.0f;
        const float generalRowStart = -92.0f;
        const float generalRowStep = 46.0f;
        const float soundRowStart = -102.0f;
        const float soundRowStep = 36.0f;
        const float controlsCategoryY = -100.0f;
        const float controlsRowStart = -68.0f;
        const float controlsRowStep = 28.0f;
        const float footerBottomMargin = 42.0f;

        // --- Background & Title ---
        HUD::add(attachToHUDGroup(
            new hudPanel(style.panel.color),
            RuntimeHudIds::kSettingsMenu
        ));

        if (auto* title = attachToHUDGroup(
            HUD::addText(
                "Settings",
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -250.0f)),
                glm::vec2(1.0f), 38
            ),
            RuntimeHudIds::kSettingsMenu)) {
            title->setColor(style.titleColor);
        }

        if (auto* subtitle = attachToHUDGroup(
            HUD::watchText(
                [&appliedSettings, &pendingSettings](std::string& out) {
                    out = settingsSubtitleText(appliedSettings, pendingSettings);
                },
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -210.0f)),
                glm::vec2(1.0f), 18
            ),
            RuntimeHudIds::kSettingsMenu)) {
            subtitle->setColor(style.subtitleColor);
        }

        // --- Tab Buttons Helper ---
        const auto addTabButton = [&](const char* text, float xOffset, RuntimeUI::SettingsMenuTab tab) {
            HUD::add(attachToHUDGroup(
                new hudButtonText(
                    text, makeCenteredMenuButtonPositioner(tabsYOffset, xOffset), 26,
                    style.inactiveTabButton.normalColor,
                    style.inactiveTabButton.hoverColor,
                    [&uiState, tab]() { uiState.activeSettingsTab = tab; },
                    [&uiState, tab]() { return uiState.activeSettingsTab != tab; }
                ),
                RuntimeHudIds::kSettingsMenu
            ));

            HUD::add(attachToHUDGroup(
                new hudButtonText(
                    text, makeCenteredMenuButtonPositioner(tabsYOffset, xOffset), 26,
                    style.activeTabButton.normalColor,
                    style.activeTabButton.hoverColor,
                    [&uiState, tab]() { uiState.activeSettingsTab = tab; },
                    [&uiState, tab]() { return uiState.activeSettingsTab == tab; }
                ),
                RuntimeHudIds::kSettingsMenu
            ));
            };

        addTabButton("General", -150.0f, RuntimeUI::SettingsMenuTab::General);
        addTabButton("Controls", 0.0f, RuntimeUI::SettingsMenuTab::Controls);
        addTabButton("Sound", 130.0f, RuntimeUI::SettingsMenuTab::Sound);

        // --- Section Title ---
        if (auto* sectionTitle = attachToHUDGroup(
            HUD::watchText(
                [&uiState](std::string& out) {
                    out = uiState.activeSettingsTab == RuntimeUI::SettingsMenuTab::General
                        ? "Display & Controls"
                        : uiState.activeSettingsTab == RuntimeUI::SettingsMenuTab::Sound
                        ? "Audio & Music"
                        : "Input Mapping";
                },
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -125.0f)),
                glm::vec2(1.0f), 22
            ),
            RuntimeHudIds::kSettingsMenu)) {
            sectionTitle->setColor(style.sectionTitleColor);
        }

        // --- Row Generation Helpers ---
        const auto addRowLabel = [&](const char* groupName, const char* text, float yOffset) {
            if (auto* label = attachToHUDGroup(
                HUD::addText(text, makeSettingsLabelLayout(yOffset, rowLabelColumn), glm::vec2(1.0f), 22),
                groupName)) {
                label->setColor(style.labelColor);
            }
            };

        const auto addArrowButton = [&](const char* groupName, const char* text,
            float xOffset, float yOffset,
            const std::function<void()>& onClick,
            const std::function<bool()>& visible = {}) {
                HUD::add(attachToHUDGroup(
                    new hudButtonText(
                        text, makeCenteredMenuButtonPositioner(yOffset, xOffset), 26,
                        style.button.normalColor,
                        style.button.hoverColor,
                        onClick,
                        visible
                    ),
                    groupName
                ));
            };

        const auto addSettingRow = [&](const char* groupName, const char* label, float yOffset,
            const std::function<void(std::string&)>& binder,
            const std::function<void()>& onPrevious,
            const std::function<void()>& onNext,
            const std::function<bool()>& enabled = {}) {
                const bool hasEnabledPredicate = static_cast<bool>(enabled);

                addRowLabel(groupName, label, yOffset);

                if (auto* value = attachToHUDGroup(
                    HUD::watchText(binder, makeSettingsValueLayout(yOffset), glm::vec2(1.0f), 22),
                    groupName)) {
                    if (hasEnabledPredicate) {
                        value->setVisualBinder(
                            [enabled,
                             valueColor = style.valueColor,
                             disabledValueColor = style.disabledValueColor](hudWatchText& text) {
                                text.setColor(enabled() ? valueColor : disabledValueColor);
                            }
                        );
                    }
                    else {
                        value->setColor(style.valueColor);
                    }
                }

                addArrowButton(groupName, "<", arrowLeftX, yOffset - 13, onPrevious,
                    hasEnabledPredicate ? enabled : std::function<bool()>{});
                addArrowButton(groupName, ">", arrowRightX, yOffset - 13, onNext,
                    hasEnabledPredicate ? enabled : std::function<bool()>{});
            };

        // --- General Tab Settings ---
        addSettingRow(
            generalGroup, "Render Distance", generalRowStart + generalRowStep * 0.0f,
            [&pendingSettings](std::string& out) { out = renderDistanceDisplayName(pendingSettings.renderDistance); },
            [&pendingSettings]() { stepRenderDistanceSelection(pendingSettings, -1); },
            [&pendingSettings]() { stepRenderDistanceSelection(pendingSettings, 1); }
        );

        addSettingRow(
            generalGroup, "Font", generalRowStart + generalRowStep * 1.0f,
            [&pendingSettings, &availableFonts](std::string& out) { out = fontSelectionText(pendingSettings, availableFonts); },
            [&pendingSettings, &availableFonts]() { stepFontSelection(pendingSettings, availableFonts, -1); },
            [&pendingSettings, &availableFonts]() { stepFontSelection(pendingSettings, availableFonts, 1); }
        );

        addSettingRow(
            generalGroup, "Mouse Sensitivity", generalRowStart + generalRowStep * 2.0f,
            [&pendingSettings](std::string& out) { out = mouseSensitivityText(pendingSettings.mouseSensitivity); },
            [&pendingSettings]() { stepMouseSensitivitySelection(pendingSettings, -ClientDefaults::kMouseSensitivityStep); },
            [&pendingSettings]() { stepMouseSensitivitySelection(pendingSettings, ClientDefaults::kMouseSensitivityStep); }
        );

        addSettingRow(
            generalGroup, "Render Scale", generalRowStart + generalRowStep * 3.0f,
            [&pendingSettings](std::string& out) { out = renderScaleText(pendingSettings.renderScale); },
            [&pendingSettings]() { stepRenderScaleSelection(pendingSettings, -ClientDefaults::kRenderScaleStep); },
            [&pendingSettings]() { stepRenderScaleSelection(pendingSettings, ClientDefaults::kRenderScaleStep); }
        );

        addSettingRow(
            generalGroup, "Resolution", generalRowStart + generalRowStep * 4.0f,
            [&pendingSettings, &availableResolutions](std::string& out) {
                out = resolutionSelectionText(pendingSettings, availableResolutions,
                    pendingSettings.windowMode == ENGINE::VIEWPORTMODE::BORDERLESS);
            },
            [&pendingSettings, &availableResolutions]() { stepResolutionSelection(pendingSettings, availableResolutions, -1); },
            [&pendingSettings, &availableResolutions]() { stepResolutionSelection(pendingSettings, availableResolutions, 1); },
            [&pendingSettings]() { return pendingSettings.windowMode != ENGINE::VIEWPORTMODE::BORDERLESS; }
        );

        addSettingRow(
            generalGroup, "Window Mode", generalRowStart + generalRowStep * 5.0f,
            [&pendingSettings](std::string& out) { out = Bootstrap::viewportModeName(pendingSettings.windowMode); },
            [&pendingSettings]() { stepWindowModeSelection(pendingSettings, -1); },
            [&pendingSettings]() { stepWindowModeSelection(pendingSettings, 1); }
        );

        addSettingRow(
            generalGroup, "VSync", generalRowStart + generalRowStep * 6.0f,
            [&pendingSettings](std::string& out) { out = onOffText(pendingSettings.vSyncEnabled); },
            [&pendingSettings]() { toggleVSyncSelection(pendingSettings); },
            [&pendingSettings]() { toggleVSyncSelection(pendingSettings); }
        );

        addSettingRow(
            generalGroup, "Show FPS Only", generalRowStart + generalRowStep * 7.0f,
            [&pendingSettings](std::string& out) { out = onOffText(pendingSettings.showFpsCounterOnly); },
            [&pendingSettings]() { toggleFpsCounterOnlySelection(pendingSettings); },
            [&pendingSettings]() { toggleFpsCounterOnlySelection(pendingSettings); }
        );

        // --- Sound Tab Settings ---
        const auto addAudioVolumeRow = [&](ENGINE::AUDIO::SoundCategoryId category, float yOffset) {
            addSettingRow(
                soundGroup, audioCategoryDisplayName(category), yOffset,
                [&pendingSettings, category](std::string& out) {
                    out = audioVolumeText(pendingSettings.audioSettings.volumeFor(category));
                },
                [&pendingSettings, category]() { stepAudioCategoryVolumeSelection(pendingSettings, category, -0.05f); },
                [&pendingSettings, category]() { stepAudioCategoryVolumeSelection(pendingSettings, category, 0.05f); }
            );
            };

        addAudioVolumeRow(ENGINE::AUDIO::SoundCategoryId::Master, soundRowStart + soundRowStep * 0.0f);
        addAudioVolumeRow(ENGINE::AUDIO::SoundCategoryId::Music, soundRowStart + soundRowStep * 1.0f);
        addAudioVolumeRow(ENGINE::AUDIO::SoundCategoryId::Ambient, soundRowStart + soundRowStep * 2.0f);
        addAudioVolumeRow(ENGINE::AUDIO::SoundCategoryId::Block, soundRowStart + soundRowStep * 3.0f);
        addAudioVolumeRow(ENGINE::AUDIO::SoundCategoryId::Ui, soundRowStart + soundRowStep * 4.0f);

        addSettingRow(
            soundGroup, "Music Enabled", soundRowStart + soundRowStep * 5.0f,
            [&pendingSettings](std::string& out) { out = onOffText(pendingSettings.audioSettings.musicEnabled); },
            [&pendingSettings]() { toggleMusicEnabledSelection(pendingSettings); },
            [&pendingSettings]() { toggleMusicEnabledSelection(pendingSettings); }
        );

        addSettingRow(
            soundGroup, "Global Mute", soundRowStart + soundRowStep * 6.0f,
            [&pendingSettings](std::string& out) { out = onOffText(pendingSettings.audioSettings.globalMute); },
            [&pendingSettings]() { toggleGlobalMuteSelection(pendingSettings); },
            [&pendingSettings]() { toggleGlobalMuteSelection(pendingSettings); }
        );

        // --- Controls Tab Settings ---
        if (auto* controlsCategoryLabel = attachToHUDGroup(
            HUD::watchText(
                [&uiState](std::string& out) {
                    const auto& categories = InputMapping::InputActionSystem::instance().catalog().categories;
                    if (categories.empty()) {
                        out = "No Controls";
                        return;
                    }
                    const std::size_t index = std::min(uiState.controlsCategoryIndex, categories.size() - 1);
                    out = categories[index].label;
                },
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, controlsCategoryY)),
                glm::vec2(1.0f), 24
            ),
            controlsGroup)) {
            controlsCategoryLabel->setColor(style.valueColor);
        }

        addArrowButton(
            controlsGroup, "<", -140.0f, controlsCategoryY - 14.0f,
            [&uiState]() {
                const auto& categories = InputMapping::InputActionSystem::instance().catalog().categories;
                if (categories.empty()) return;
                const std::size_t count = categories.size();
                uiState.controlsCategoryIndex = (uiState.controlsCategoryIndex + count - 1) % count;
                uiState.controlsWarningMessage.clear();
            }
        );

        addArrowButton(
            controlsGroup, ">", 140.0f, controlsCategoryY - 14.0f,
            [&uiState]() {
                const auto& categories = InputMapping::InputActionSystem::instance().catalog().categories;
                if (categories.empty()) return;
                uiState.controlsCategoryIndex = (uiState.controlsCategoryIndex + 1) % categories.size();
                uiState.controlsWarningMessage.clear();
            }
        );

        const auto controlsLabelLayout = [](float yOffset) {
            return makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER_RIGHT, glm::vec2(-170.0f, yOffset));
            };

        const auto controlsValueLayout = [](float yOffset) {
            return makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, yOffset));
            };

        for (int rowIndex = 0; rowIndex < kControlsVisibleRows; ++rowIndex) {
            const float rowY = controlsRowStart + controlsRowStep * static_cast<float>(rowIndex);

            if (auto* label = attachToHUDGroup(
                HUD::watchText(
                    [&uiState, rowIndex](std::string& out) {
                        const auto* action = controlsActionForRow(uiState, rowIndex);
                        out = action ? action->label : std::string{};
                    },
                    controlsLabelLayout(rowY), glm::vec2(1.0f), 18
                ),
                controlsGroup)) {
                label->setColor(style.labelColor);
            }

            if (auto* binding = attachToHUDGroup(
                HUD::watchText(
                    [&pendingSettings, &uiState, rowIndex](std::string& out) {
                        const auto* action = controlsActionForRow(uiState, rowIndex);
                        out = action
                            ? InputMapping::InputActionSystem::instance()
                            .bindingDisplayText(action->id, &pendingSettings.controlOverrides)
                            : std::string{};
                    },
                    controlsValueLayout(rowY), glm::vec2(1.0f), 18
                ),
                controlsGroup)) {
                binding->setColor(style.valueColor);
            }

            HUD::add(attachToHUDGroup(
                new hudButtonText(
                    "Rebind", makeCenteredMenuButtonPositioner(rowY - 12.0f, 185.0f), 18,
                    style.button.normalColor,
                    style.button.hoverColor,
                    [&uiState, rowIndex]() {
                        const auto* action = controlsActionForRow(uiState, rowIndex);
                        if (!action) return;
                        uiState.controlsCaptureOpen = true;
                        uiState.controlsCaptureIgnoreMouseLeft = true;
                        uiState.controlsCaptureActionId = action->id;
                        uiState.controlsWarningMessage.clear();
                    },
                    [&uiState, rowIndex]() { return controlsActionForRow(uiState, rowIndex) != nullptr; }
                ),
                controlsGroup
            ));

            HUD::add(attachToHUDGroup(
                new hudButtonText(
                    "Clear", makeCenteredMenuButtonPositioner(rowY - 12.0f, 300.0f), 18,
                    style.button.normalColor,
                    style.button.hoverColor,
                    [&pendingSettings, &uiState, rowIndex]() {
                        const auto* action = controlsActionForRow(uiState, rowIndex);
                        if (!action) return;
                        InputMapping::InputActionSystem::instance().clearOverride(action->id, pendingSettings.controlOverrides);
                        uiState.controlsWarningMessage.clear();
                    },
                    [&uiState, rowIndex]() { return controlsActionForRow(uiState, rowIndex) != nullptr; }
                ),
                controlsGroup
            ));
        }

        if (auto* controlsWarning = attachToHUDGroup(
            HUD::watchText(
                [&uiState](std::string& out) { out = uiState.controlsWarningMessage; },
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, 205.0f)),
                glm::vec2(1.0f), 16
            ),
            controlsGroup)) {
            controlsWarning->setColor(style.warningColor);
        }

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Reset Category", makeBottomMenuButtonPositioner(92.0f, -125.0f), 22,
                style.button.normalColor,
                style.button.hoverColor,
                [&pendingSettings, &uiState]() {
                    const auto* category = selectedControlsCategory(uiState);
                    if (!category) return;
                    InputMapping::InputActionSystem::instance().resetCategory(category->id, pendingSettings.controlOverrides);
                    uiState.controlsWarningMessage.clear();
                }
            ),
            controlsGroup
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Reset All", makeBottomMenuButtonPositioner(92.0f, 125.0f), 22,
                style.button.normalColor,
                style.button.hoverColor,
                [&pendingSettings, &uiState]() {
                    InputMapping::InputActionSystem::instance().resetAll(pendingSettings.controlOverrides);
                    uiState.controlsWarningMessage.clear();
                }
            ),
            controlsGroup
        ));

        // --- Footer Actions ---
        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Apply", makeBottomMenuButtonPositioner(footerBottomMargin, -90.0f), 30,
                style.footerButton.normalColor,
                style.footerButton.hoverColor,
                [&player, &worldStack, &renderer, &audioController, &uiState, &appliedSettings, &pendingSettings]() {
                    applyPendingSettings(player, worldStack, renderer, audioController, uiState,
                        appliedSettings, pendingSettings);
                }
            ),
            RuntimeHudIds::kSettingsMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Back", makeBottomMenuButtonPositioner(footerBottomMargin, 90.0f), 30,
                style.footerButton.normalColor,
                style.footerButton.hoverColor,
                [&uiState, &appliedSettings, &pendingSettings]() {
                    requestCloseSettingsMenu(uiState, appliedSettings, pendingSettings);
                }
            ),
            RuntimeHudIds::kSettingsMenu
        ));
    }

#pragma endregion

#pragma region 8. Settings Confirmation Menu
    // --- 8. Settings Confirmation Menu ---

    // Confirmation dialog shown when leaving settings with unapplied changes.
    void addSettingsDiscardConfirmHUD(Player& player, WorldStack& worldStack, Renderer& renderer,
        GameAudioController& audioController,
        GameSettings& appliedSettings, GameSettings& pendingSettings,
        RuntimeUiState& uiState) {
        const SettingsDiscardConfirmHUDStyle& style =
            HUDStyles::kSettingsDiscardConfirm;

        HUD::add(attachToHUDGroup(
            new hudPanel(style.panel.color),
            RuntimeHudIds::kSettingsConfirmMenu
        ));

        if (auto* title = attachToHUDGroup(
            HUD::addText(
                "Unsaved Changes",
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -60.0f)),
                glm::vec2(1.0f), 34
            ),
            RuntimeHudIds::kSettingsConfirmMenu)) {
            title->setColor(style.titleColor);
        }

        if (auto* message = attachToHUDGroup(
            HUD::addText(
                "Apply the pending settings before leaving?",
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -10.0f)),
                glm::vec2(1.0f), 20
            ),
            RuntimeHudIds::kSettingsConfirmMenu)) {
            message->setColor(style.messageColor);
        }

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Apply", makeCenteredMenuButtonPositioner(70.0f, -170.0f), 28,
                style.button.normalColor,
                style.button.hoverColor,
                [&player, &worldStack, &renderer, &audioController, &uiState, &appliedSettings, &pendingSettings]() {
                    applyPendingSettings(player, worldStack, renderer, audioController, uiState,
                        appliedSettings, pendingSettings);
                    uiState.settingsDiscardConfirmOpen = false;
                    uiState.settingsMenuOpen = false;
                }
            ),
            RuntimeHudIds::kSettingsConfirmMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Discard", makeCenteredMenuButtonPositioner(70.0f, 0.0f), 28,
                style.button.normalColor,
                style.button.hoverColor,
                [&uiState, &appliedSettings, &pendingSettings]() {
                    closeSettingsMenu(uiState, appliedSettings, pendingSettings);
                }
            ),
            RuntimeHudIds::kSettingsConfirmMenu
        ));

        HUD::add(attachToHUDGroup(
            new hudButtonText(
                "Cancel", makeCenteredMenuButtonPositioner(70.0f, 170.0f), 28,
                style.button.normalColor,
                style.button.hoverColor,
                [&uiState]() { uiState.settingsDiscardConfirmOpen = false; }
            ),
            RuntimeHudIds::kSettingsConfirmMenu
        ));
    }

#pragma endregion

#pragma region 9. Settings Controls Capture
    // --- 9. Settings Controls Capture ---

    void addSettingsControlsCaptureHUD(RuntimeUiState& uiState) {
        const SettingsControlsCaptureHUDStyle& style =
            HUDStyles::kSettingsControlsCapture;

        HUD::add(attachToHUDGroup(
            new hudPanel(style.panel.color),
            RuntimeHudIds::kSettingsControlsCaptureMenu
        ));

        if (auto* title = attachToHUDGroup(
            HUD::addText(
                "Rebind Control",
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -58.0f)),
                glm::vec2(1.0f), 30
            ),
            RuntimeHudIds::kSettingsControlsCaptureMenu)) {
            title->setColor(style.titleColor);
        }

        if (auto* actionLabel = attachToHUDGroup(
            HUD::watchText(
                [&uiState](std::string& out) {
                    const std::string label = controlsCaptureActionLabel(uiState);
                    out = label.empty() ? std::string{} : "Action: " + label;
                },
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, -18.0f)),
                glm::vec2(1.0f), 20
            ),
            RuntimeHudIds::kSettingsControlsCaptureMenu)) {
            actionLabel->setColor(style.actionLabelColor);
        }

        if (auto* prompt = attachToHUDGroup(
            HUD::addText(
                "Press a key or mouse button... Esc to cancel",
                makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f, 22.0f)),
                glm::vec2(1.0f), 18
            ),
            RuntimeHudIds::kSettingsControlsCaptureMenu)) {
            prompt->setColor(style.promptColor);
        }
    }

#pragma endregion

#pragma region 10. Inventory Menu
    // --- 10. Inventory Menu ---

    // Full inventory menu visuals plus 3D slot previews.
    void addInventoryMenuHUD(Player& player, Renderer& renderer, WorldStack& worldStack) {
        HUD::add(attachToHUDGroup(
            new hudInventoryMenu(&player, InventoryMenuVisualPart::BACKGROUND),
            RuntimeHudIds::kInventoryMenu, 0
        ));

        HUD::add(attachToHUDGroup(
            new hudInventoryMenu(&player, InventoryMenuVisualPart::COUNTS),
            RuntimeHudIds::kInventoryMenu, 1
        ));

        HUD::add(attachToHUDGroup(
            new hudInventoryMenuPreview(
                &renderer,
                &player,
                &worldStack,
                InventoryMenuPreviewPart::SLOTS
            ),
            RuntimeHudIds::kInventoryMenu, 2
        ));

        HUD::add(attachToHUDGroup(
            new hudInventoryMenu(&player, InventoryMenuVisualPart::TOOLTIP),
            RuntimeHudIds::kInventoryMenu, 3
        ));

        HUD::add(attachToHUDGroup(
            new hudInventoryMenuPreview(
                &renderer,
                &player,
                &worldStack,
                InventoryMenuPreviewPart::TOOLTIP
            ),
            RuntimeHudIds::kInventoryMenu, 4
        ));
    }

#pragma endregion

#pragma region 11. Pause Helpers
    // --- 11. Pause Helpers ---

    // Escape either cancels portal editing or toggles the pause state.
    void togglePauseOrCancelPortalEdit(hudPortalInfo* portalInfo) {
        if (!portalInfo || !portalInfo->cancelEditing()) {
            ENGINE::TOGGLEPAUSED();
        }
    }

#pragma endregion

}  // namespace RuntimeUI::Detail

namespace RuntimeUI {

#pragma region 13. Runtime Sync
    // --- 13. Runtime Sync ---

    // Keeps HUD group visibility aligned with pause/settings state.
    void syncHudMenuState(RuntimeUiState& uiState) {
        const bool paused = ENGINE::ISPAUSED();
        if (!paused) {
            uiState.settingsMenuOpen = false;
            uiState.settingsDiscardConfirmOpen = false;
        }

        const bool settingsVisible = paused && uiState.settingsMenuOpen && !uiState.settingsDiscardConfirmOpen;

        HUD::setGroupEnabled(RuntimeHudIds::kPauseMenu, paused && !uiState.settingsMenuOpen);
        HUD::setGroupEnabled(RuntimeHudIds::kSettingsMenu, settingsVisible);

        HUD::setGroupEnabled(
            RuntimeHudIds::kSettingsGeneralTab,
            settingsVisible && uiState.activeSettingsTab == SettingsMenuTab::General
        );

        HUD::setGroupEnabled(
            RuntimeHudIds::kSettingsSoundTab,
            settingsVisible && uiState.activeSettingsTab == SettingsMenuTab::Sound
        );

        HUD::setGroupEnabled(
            RuntimeHudIds::kSettingsControlsTab,
            settingsVisible && uiState.activeSettingsTab == SettingsMenuTab::Controls
        );

        HUD::setGroupEnabled(
            RuntimeHudIds::kSettingsConfirmMenu,
            paused && uiState.settingsMenuOpen && uiState.settingsDiscardConfirmOpen
        );

        HUD::setGroupEnabled(
            RuntimeHudIds::kSettingsControlsCaptureMenu,
            settingsVisible && uiState.controlsCaptureOpen && !uiState.settingsDiscardConfirmOpen
        );
    }

    // Switches between the full debug text and the compact FPS-only HUD.
    void syncDebugHudState(const RuntimeUiState& uiState, const GameSettings& settings) {
        HUD::setGroupEnabled(RuntimeHudIds::kDebugInfo, uiState.debugTextVisible);
        HUD::setGroupEnabled(
            RuntimeHudIds::kFpsOnly,
            settings.showFpsCounterOnly && !uiState.debugTextVisible
        );
    }

    void syncSaveToastState(const RuntimeUiState& uiState) {
        HUD::setGroupEnabled(RuntimeHudIds::kSaveToast, uiState.saveToastTimer > 0.0f);
    }

    void syncCursorVisibility(const Player& player, const hudPortalTracker* portalTracker) {
        Input::setCursorVisible(
            ENGINE::ISPAUSED() ||
            player.isInventoryOpen() ||
            Input::hasUiFocus() ||
            (portalTracker && portalTracker->isMenuOpen())
        );
    }

#pragma endregion

#pragma region 14. Setup HUD Main
    // --- 14. Setup HUD Main ---

    // Builds the full runtime HUD tree in one place during startup/rebuild.
    hudPortalInfo* setupHUD(Player& player, WorldStack& worldStack, Renderer& renderer,
        GameAudioController& audioController,
        GLFWwindow* window, GameSettings& appliedSettings,
        GameSettings& pendingSettings,
        const std::vector<std::string>& availableFonts,
        const std::vector<glm::ivec2>& availableResolutions,
        RuntimeUiState& uiState,
        hudPortalTracker** outPortalTracker) {

        HUD::setDefaultFont(appliedSettings.fontAssetPath());

        Detail::createHUDGroups();
        Detail::addFpsOnlyHUD();
        Detail::addDebugHUD(worldStack, player);
        //Detail::addPlayerStatusHUD(player);

        auto* portalInfo = new hudPortalInfo(&player, &worldStack, 18);
        HUD::add(portalInfo);

        auto* imgCrosshair = new hudImage(
            ClientAssets::kCrosshairTexture,
            makeHUDLayout(HUDAnchor::CENTER, HUDAnchor::CENTER, glm::vec2(0.0f)),
            glm::vec2(16.0f, 16.0f), ImageType::STRETCH
        );
        HUD::add(imgCrosshair);

        Detail::addHotbarHUD(player, renderer, worldStack);
        Detail::addSaveToastHUD(uiState);

        hudPortalTracker* portalTracker = nullptr;
        Detail::addPortalTrackerHUD(player, worldStack, portalTracker);

        Detail::addInventoryMenuHUD(player, renderer, worldStack);
        Detail::addPauseMenuHUD(window, uiState, appliedSettings, pendingSettings);

        Detail::addSettingsMenuHUD(
            player, worldStack, renderer, audioController, appliedSettings,
            pendingSettings, availableFonts, availableResolutions, uiState
        );

        Detail::addSettingsDiscardConfirmHUD(
            player, worldStack, renderer, audioController,
            appliedSettings, pendingSettings, uiState
        );

        Detail::addSettingsControlsCaptureHUD(uiState);

        syncHudMenuState(uiState);
        syncDebugHudState(uiState, appliedSettings);

        if (outPortalTracker) {
            *outPortalTracker = portalTracker;
        }

        return portalInfo;
    }

#pragma endregion

}  // namespace RuntimeUI
