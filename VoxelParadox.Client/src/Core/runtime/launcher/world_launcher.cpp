// World launcher:
// - presents the pre-game world selection screen
// - creates or loads a world session
// - keeps all launcher rendering inside the game's HUD system

#include "runtime/launcher/world_launcher.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "audio/game_audio_controller.hpp"
#include "input/input_action_system.hpp"
#include "render/core/renderer.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_world_launcher.hpp"

namespace WorldLauncher {
namespace {

constexpr double kMinimumLoadingScreenSeconds = 0.20;

struct LauncherTaskResult {
  bool success = false;
  std::string error;
  WorldSaveService::WorldSession session{};
  std::filesystem::path worldDirectory{};
};

LauncherTaskResult runCreateTask(
    const std::string& worldName,
    std::optional<std::uint32_t> requestedRootSeed,
    std::optional<int> requestedRootDepth,
    const BiomeSelection& rootBiomeSelection) {
  LauncherTaskResult result;
  if (!WorldSaveService::createWorld(worldName, rootBiomeSelection, result.session,
                                     &result.error, requestedRootSeed,
                                     requestedRootDepth)) {
    return result;
  }

  result.success = true;
  return result;
}

LauncherTaskResult runLoadTask(const std::filesystem::path& worldDirectory) {
  LauncherTaskResult result;
  if (!WorldSaveService::loadPlayerAndWorldSession(worldDirectory,
                                                   result.session, &result.error)) {
    return result;
  }

  result.success = true;
  return result;
}

LauncherTaskResult runDeleteTask(const std::filesystem::path& worldDirectory) {
  LauncherTaskResult result;
  if (!WorldSaveService::deleteWorld(worldDirectory, &result.error)) {
    return result;
  }

  result.success = true;
  return result;
}

LauncherTaskResult runRenameTask(const std::filesystem::path& worldDirectory,
                                 const std::string& worldName) {
  LauncherTaskResult result;
  if (!WorldSaveService::renameWorld(worldDirectory, worldName,
                                     &result.worldDirectory, &result.error)) {
    return result;
  }

  result.success = true;
  return result;
}

void restoreGameplayInput() {
  Input::enableTextInput(false);
  Input::setFocusMode(Input::FocusMode::GAMEPLAY);
  Input::setCursorVisible(false);
  ENGINE::SETPAUSED(false);
}

}  // namespace

RunResult run(GLFWwindow* window, const BiomeSelection& rootBiomeSelection,
              WorldSaveService::WorldSession& outSession,
              std::string* outError,
              GameAudioController* audioController,
              Renderer* renderer) {
  if (!window) {
    if (outError) {
      *outError = "Launcher window is not available.";
    }
    return RunResult::Error;
  }

  HUD::clear();
  HUD::setVisible(true);

  auto* launcherHud = new hudWorldLauncher();
  launcherHud->setWorlds(WorldSaveService::listWorlds());
  HUD::add(launcherHud);

  bool loading = false;
  std::future<LauncherTaskResult> taskFuture;
  LauncherTaskResult completedTask{};
  bool hasCompletedTask = false;
  double loadingStartedAt = 0.0;
  hudWorldLauncher::ActionType loadingAction =
      hudWorldLauncher::ActionType::None;
  std::string statusMessage;

  Input::setFocusMode(Input::FocusMode::UI);
  Input::setCursorVisible(true);
  Input::enableTextInput(true);

  double lastTime = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {
    const double currentTime = glfwGetTime();
    const float rawDt = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    ENGINE::UPDATE(currentTime, rawDt);
    Input::update();
    auto& inputActions = InputMapping::InputActionSystem::instance();
    inputActions.setCaptureMode(false);
    inputActions.setActiveContexts(
        {InputMapping::InputContext::UiNavigation,
         InputMapping::InputContext::Ui});

    if (loading && taskFuture.valid() && !hasCompletedTask) {
      const std::future_status status =
          taskFuture.wait_for(std::chrono::milliseconds(0));
      if (status == std::future_status::ready) {
        completedTask = taskFuture.get();
        hasCompletedTask = true;
      }
    }

    if (loading && hasCompletedTask &&
        (currentTime - loadingStartedAt) >= kMinimumLoadingScreenSeconds) {
      loading = false;
      launcherHud->setLoading(false);
      if (completedTask.success) {
        if (loadingAction == hudWorldLauncher::ActionType::DeleteWorld ||
            loadingAction == hudWorldLauncher::ActionType::RenameWorld) {
          statusMessage.clear();
          launcherHud->setStatusMessage(statusMessage);
          launcherHud->setWorlds(WorldSaveService::listWorlds());
          if (loadingAction == hudWorldLauncher::ActionType::RenameWorld &&
              !completedTask.worldDirectory.empty()) {
            launcherHud->setSelectedWorldDirectory(completedTask.worldDirectory);
          }
          hasCompletedTask = false;
          loadingAction = hudWorldLauncher::ActionType::None;
          continue;
        }

        outSession = std::move(completedTask.session);
        if (outError) {
          outError->clear();
        }
        restoreGameplayInput();
        HUD::clear();
        return RunResult::StartWorld;
      }

      statusMessage = completedTask.error;
      launcherHud->setStatusMessage(statusMessage);
      launcherHud->setWorlds(WorldSaveService::listWorlds());
      hasCompletedTask = false;
      loadingAction = hudWorldLauncher::ActionType::None;
    }

    if (!loading) {
      const hudWorldLauncher::ActionRequest request = launcherHud->consumeRequest();
      switch (request.type) {
      case hudWorldLauncher::ActionType::CreateWorld:
        loading = true;
        loadingStartedAt = currentTime;
        hasCompletedTask = false;
        loadingAction = request.type;
        statusMessage.clear();
        launcherHud->setStatusMessage(statusMessage);
        launcherHud->setLoading(true);
        taskFuture = std::async(std::launch::async, [request, rootBiomeSelection]() {
          return runCreateTask(
              request.worldName,
              request.hasCustomSeed
                  ? std::optional<std::uint32_t>(request.customSeed)
                  : std::nullopt,
              request.hasCustomDepth
                  ? std::optional<int>(request.customDepth)
                  : std::nullopt,
              rootBiomeSelection);
        });
        break;
      case hudWorldLauncher::ActionType::LoadWorld:
        loading = true;
        loadingStartedAt = currentTime;
        hasCompletedTask = false;
        loadingAction = request.type;
        statusMessage.clear();
        launcherHud->setStatusMessage(statusMessage);
        launcherHud->setLoading(true);
        taskFuture = std::async(std::launch::async, [request]() {
          return runLoadTask(request.worldDirectory);
        });
        break;
      case hudWorldLauncher::ActionType::DeleteWorld:
        loading = true;
        loadingStartedAt = currentTime;
        hasCompletedTask = false;
        loadingAction = request.type;
        statusMessage.clear();
        launcherHud->setStatusMessage(statusMessage);
        launcherHud->setLoading(true);
        taskFuture = std::async(std::launch::async, [request]() {
          return runDeleteTask(request.worldDirectory);
        });
        break;
      case hudWorldLauncher::ActionType::RenameWorld:
        loading = true;
        loadingStartedAt = currentTime;
        hasCompletedTask = false;
        loadingAction = request.type;
        statusMessage.clear();
        launcherHud->setStatusMessage(statusMessage);
        launcherHud->setLoading(true);
        taskFuture = std::async(std::launch::async, [request]() {
          return runRenameTask(request.worldDirectory, request.worldName);
        });
        break;
      case hudWorldLauncher::ActionType::BackToMenu:
        Input::enableTextInput(false);
        Input::setFocusMode(Input::FocusMode::UI);
        Input::setCursorVisible(true);
        HUD::clear();
        if (outError) {
          outError->clear();
        }
        return RunResult::BackToMenu;
      case hudWorldLauncher::ActionType::None:
      default:
        break;
      }
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0) {
      if (audioController) {
        audioController->syncMenuFrame(false, rawDt);
      }
      glViewport(0, 0, framebufferWidth, framebufferHeight);
      glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      if (renderer) {
        renderer->renderMenuScreenBackground(
            glm::ivec2(framebufferWidth, framebufferHeight),
            static_cast<float>(currentTime));
      }
      HUD::update(framebufferWidth, framebufferHeight);
      HUD::render(framebufferWidth, framebufferHeight);
    }

    glfwSwapBuffers(window);
  }

  restoreGameplayInput();
  HUD::clear();
  if (outError) {
    outError->clear();
  }
  return RunResult::ExitGame;
}

}  // namespace WorldLauncher
