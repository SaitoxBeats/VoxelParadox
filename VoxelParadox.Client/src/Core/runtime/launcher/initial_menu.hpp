#pragma once

#include <string>

#include "runtime/app/runtime_app_internal.hpp"

struct GLFWwindow;

class GameAudioController;
class Renderer;

namespace InitialMenu {

enum class RunResult {
    StartGame = 0,
    ExitGame,
    Error,
};

RunResult run(GLFWwindow* window, Renderer& renderer,
              GameAudioController& audioController,
              RuntimeAppInternal::RuntimeSettingsBundle& settingsBundle,
              std::string* outError = nullptr);

} // namespace InitialMenu
