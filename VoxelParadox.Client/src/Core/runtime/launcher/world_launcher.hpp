// World launcher:
// - lists created worlds
// - creates new worlds from a typed name
// - loads a world session before gameplay starts

#pragma once

#include <memory>
#include <string>

#include "world/biome/biome.hpp"
#include "world/persistence/world_save_service.hpp"

struct GLFWwindow;
class GameAudioController;
class Renderer;

namespace WorldLauncher {

enum class RunResult {
  StartWorld = 0,
  BackToMenu,
  ExitGame,
  Error,
};

RunResult run(GLFWwindow* window, const BiomeSelection& rootBiomeSelection,
              WorldSaveService::WorldSession& outSession,
              std::string* outError = nullptr,
              GameAudioController* audioController = nullptr,
              Renderer* renderer = nullptr);

}  // namespace WorldLauncher
