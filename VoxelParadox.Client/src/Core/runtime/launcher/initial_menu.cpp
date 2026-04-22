#include "runtime/launcher/initial_menu.hpp"

#include <string>

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "audio/game_audio_controller.hpp"
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "input/input_action_system.hpp"
#include "player/player.hpp"
#include "render/core/renderer.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_initial_menu.hpp"
#include "runtime/ui/runtime_ui_internal.hpp"
#include "world/persistence/world_stack.hpp"

namespace InitialMenu {
namespace {

void rebuildMenuHud(GLFWwindow* window, Renderer& renderer,
                    GameAudioController& audioController,
                    RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle,
                    Player& settingsPlayer, WorldStack& settingsWorldStack,
                    hudInitialMenu*& menuHud) {
    (void)window;

    HUD::clear();
    HUD::setDefaultFont(settingsBundle.applied.fontAssetPath());

    menuHud = new hudInitialMenu();
    menuHud->setEnabled(!settingsBundle.uiState.settingsMenuOpen &&
                        !settingsBundle.uiState.settingsDiscardConfirmOpen);
    HUD::add(menuHud);

    RuntimeUI::Detail::createHUDGroups();
    RuntimeUI::Detail::addSettingsMenuHUD(
        settingsPlayer, settingsWorldStack, renderer, audioController,
        settingsBundle.applied, settingsBundle.pending,
        settingsBundle.availableFonts, settingsBundle.availableResolutions,
        settingsBundle.uiState
    );
    RuntimeUI::Detail::addSettingsDiscardConfirmHUD(
        settingsPlayer, settingsWorldStack, renderer, audioController,
        settingsBundle.applied, settingsBundle.pending, settingsBundle.uiState
    );
    RuntimeUI::Detail::addSettingsControlsCaptureHUD(settingsBundle.uiState);
    RuntimeUI::syncHudMenuState(settingsBundle.uiState);
}

void renderMenuFrame(GLFWwindow* window, Renderer& renderer, double currentTime) {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }

    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.renderMenuScreenBackground(
        glm::ivec2(framebufferWidth, framebufferHeight),
        static_cast<float>(currentTime)
    );
    HUD::update(framebufferWidth, framebufferHeight);
    HUD::render(framebufferWidth, framebufferHeight);
}

} // namespace

RunResult run(GLFWwindow* window, Renderer& renderer,
              GameAudioController& audioController,
              RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle,
              std::string* outError) {
    if (!window) {
        if (outError) {
            *outError = "Initial menu window is not available.";
        }
        return RunResult::Error;
    }

    settingsBundle.uiState.settingsMenuOpen = false;
    settingsBundle.uiState.settingsDiscardConfirmOpen = false;
    settingsBundle.uiState.hudRebuildRequested = false;
    settingsBundle.uiState.returnToLauncherRequested = false;

    HUD::setVisible(true);
    ENGINE::SETPAUSED(true);

    Player settingsPlayer;
    settingsPlayer.camera.sensitivity = settingsBundle.applied.mouseSensitivity;
    settingsPlayer.normalFov = settingsBundle.applied.fieldOfView;
    settingsPlayer.camera.baseFov = settingsBundle.applied.fieldOfView;

    WorldStack settingsWorldStack;
    settingsWorldStack.setRenderDistancePreset(settingsBundle.applied.renderDistance);

    hudInitialMenu* menuHud = nullptr;
    rebuildMenuHud(window, renderer, audioController, settingsBundle,
                   settingsPlayer, settingsWorldStack, menuHud);

    Input::setFocusMode(Input::FocusMode::UI);
    Input::setCursorVisible(true);
    Input::enableTextInput(false);

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        const double currentTime = glfwGetTime();
        const float rawDt = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        ENGINE::UPDATE(currentTime, rawDt);
        Input::update();

        auto& inputActions = InputMapping::InputActionSystem::instance();
        inputActions.setCaptureMode(settingsBundle.uiState.controlsCaptureOpen);
        if (settingsBundle.uiState.controlsCaptureOpen) {
            inputActions.clearActiveContexts();
        } else {
            inputActions.setActiveContexts(
                {InputMapping::InputContext::UiNavigation,
                 InputMapping::InputContext::Ui});
        }

        if (settingsBundle.uiState.settingsMenuOpen ||
            settingsBundle.uiState.settingsDiscardConfirmOpen ||
            settingsBundle.uiState.controlsCaptureOpen) {
            RuntimeUI::handleGlobalShortcuts(
                nullptr, nullptr, settingsWorldStack, settingsPlayer, audioController,
                settingsBundle.uiState, settingsBundle.applied, settingsBundle.pending
            );
        }

        RuntimeUI::syncHudMenuState(settingsBundle.uiState);
        if (menuHud) {
            menuHud->setEnabled(!settingsBundle.uiState.settingsMenuOpen &&
                                !settingsBundle.uiState.settingsDiscardConfirmOpen);
        }

        if (settingsBundle.uiState.hudRebuildRequested) {
            settingsBundle.uiState.hudRebuildRequested = false;
            rebuildMenuHud(window, renderer, audioController, settingsBundle,
                           settingsPlayer, settingsWorldStack, menuHud);
        }

        audioController.syncMenuFrame(settingsBundle.uiState.settingsMenuOpen, rawDt);

        if (!settingsBundle.uiState.settingsMenuOpen &&
            !settingsBundle.uiState.settingsDiscardConfirmOpen && menuHud) {
            switch (menuHud->consumeAction()) {
            case hudInitialMenu::ActionType::StartGame:
                audioController.onMenuActionSelected();
                HUD::clear();
                ENGINE::SETPAUSED(false);
                if (outError) {
                    outError->clear();
                }
                return RunResult::StartGame;
            case hudInitialMenu::ActionType::OpenSettings:
                audioController.onMenuActionSelected();
                RuntimeUI::Detail::openSettingsMenu(
                    settingsBundle.uiState, settingsBundle.applied,
                    settingsBundle.pending
                );
                break;
            case hudInitialMenu::ActionType::ExitGame:
                audioController.onMenuActionSelected();
                HUD::clear();
                ENGINE::SETPAUSED(false);
                if (outError) {
                    outError->clear();
                }
                return RunResult::ExitGame;
            case hudInitialMenu::ActionType::None:
            default:
                break;
            }
        }

        renderMenuFrame(window, renderer, currentTime);
        glfwSwapBuffers(window);
    }

    HUD::clear();
    ENGINE::SETPAUSED(false);
    if (outError) {
        outError->clear();
    }
    return RunResult::ExitGame;
}

} // namespace InitialMenu
