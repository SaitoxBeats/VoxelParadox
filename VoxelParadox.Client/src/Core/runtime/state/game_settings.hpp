// Arquivo: VoxelParadox.Client/src/Core/runtime/state/game_settings.hpp
// Papel: declara "game settings" dentro do subsistema "runtime" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes

#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "client_defaults.hpp"
#include "audio/audio_types.hpp"
#include "engine/engine.hpp"
#include "input/input_binding.hpp"
#include "world/persistence/world_stack.hpp"
#pragma endregion

#pragma region GameSettingsApi
struct GameSettings {
  std::string fontFile = ClientDefaults::kDefaultFontFile;
  glm::ivec2 resolution{ClientDefaults::kDefaultWindowedResolution};
  WorldStack::RenderDistancePreset renderDistance =
      WorldStack::RenderDistancePreset::NORMAL;
  float mouseSensitivity = 0.002f;
  float fieldOfView = ClientDefaults::kDefaultFieldOfView;
  float renderScale = ClientDefaults::kDefaultRenderScale;
  ENGINE::VIEWPORTMODE windowMode = ENGINE::VIEWPORTMODE::WINDOWMODE;
  bool vSyncEnabled = false;
  bool showFpsCounterOnly = false;
  bool advancedLightingEnabled = true;
  ENGINE::AUDIO::AudioSettings audioSettings{};
  InputMapping::ControlBindingOverrides controlOverrides{};

  std::string fontAssetPath() const;
  std::string fontDisplayName() const;


  void sanitize(const std::vector<std::string>& availableFonts,
                const std::vector<glm::ivec2>& availableResolutions = {});
  bool save(std::string* outError = nullptr) const;


  static GameSettings load(std::string* outError = nullptr);
  static std::vector<std::string> availableFonts();
  static std::vector<glm::ivec2> availableDisplayResolutions();
  static std::filesystem::path settingsDirectory();
  static std::filesystem::path settingsFilePath();
};
#pragma endregion
