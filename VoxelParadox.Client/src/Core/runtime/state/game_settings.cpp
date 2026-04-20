// Arquivo: VoxelParadox.Client/src/Core/runtime/game_settings.cpp
// Papel: implementa "game settings" dentro do subsistema "runtime" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "runtime/state/game_settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <system_error>

#include <windows.h>

#include <nlohmann/json.hpp>

#include "client_assets.hpp"
#include "client_defaults.hpp"
#include "path/app_paths.hpp"
#pragma endregion

#pragma region PlatformAndJsonIncludes
#ifdef _WIN32
#ifdef FAR
#undef FAR
#endif
#ifdef NEAR
#undef NEAR
#endif
#endif
#pragma endregion

#pragma region SettingsParsingHelpers
namespace {

// Reading guide:
// - load/save handle disk I/O
// - sanitize repairs invalid or old config values
// - helper functions below convert user-facing tokens to internal enums

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

const char* renderDistanceToken(WorldStack::RenderDistancePreset preset) {
  switch (preset) {
  case WorldStack::RenderDistancePreset::SHORT:
    return "short";
  case WorldStack::RenderDistancePreset::NORMAL:
    return "normal";
  case WorldStack::RenderDistancePreset::FAR:
    return "far";
  case WorldStack::RenderDistancePreset::ULTRA:
    return "ultra";
  }
  return "normal";
}

WorldStack::RenderDistancePreset parseRenderDistanceToken(
    const std::string& token) {
  const std::string lower = toLower(token);
  if (lower == "short") {
    return WorldStack::RenderDistancePreset::SHORT;
  }
  if (lower == "far") {
    return WorldStack::RenderDistancePreset::FAR;
  }
  if (lower == "ultra") {
    return WorldStack::RenderDistancePreset::ULTRA;
  }
  return WorldStack::RenderDistancePreset::NORMAL;
}


const char* windowModeToken(ENGINE::VIEWPORTMODE mode) {
  switch (mode) {
  case ENGINE::VIEWPORTMODE::WINDOWMODE:
    return "windowed";
  case ENGINE::VIEWPORTMODE::BORDERLESS:
    return "borderless";
  case ENGINE::VIEWPORTMODE::FULLSCREEN:
    return "fullscreen";
  }
  return "borderless";
}


ENGINE::VIEWPORTMODE parseWindowModeToken(const std::string& token) {
  const std::string lower = toLower(token);
  if (lower == "windowed") {
    return ENGINE::VIEWPORTMODE::WINDOWMODE;
  }
  if (lower == "fullscreen") {
    return ENGINE::VIEWPORTMODE::FULLSCREEN;
  }
  return ENGINE::VIEWPORTMODE::BORDERLESS;
}


bool isTtfFile(const std::filesystem::path& path) {
  return toLower(path.extension().string()) == ".ttf";
}


std::string sanitizeFontFileName(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  return std::filesystem::path(value).filename().string();
}


bool sameResolution(const glm::ivec2& a, const glm::ivec2& b) {
  return a.x == b.x && a.y == b.y;
}


bool isValidResolution(const glm::ivec2& resolution) {
  return resolution.x > 0 && resolution.y > 0;
}


bool hasResolution(const std::vector<glm::ivec2>& resolutions,
                   const glm::ivec2& target) {
  return std::find_if(resolutions.begin(), resolutions.end(),
                      [&target](const glm::ivec2& candidate) {
                        return sameResolution(candidate, target);
                      }) != resolutions.end();
}

int sanitizeAntiAliasingSamples(int samples) {
  for (const int option : ClientDefaults::kAntiAliasingSampleOptions) {
    if (option == samples) {
      return option;
    }
  }
  return ClientDefaults::kDefaultAntiAliasingSamples;
}

int parseAntiAliasingSamplesToken(const std::string& token) {
  const std::string lower = toLower(token);
  if (lower == "off") {
    return ClientDefaults::kDefaultAntiAliasingSamples;
  }

  if (!lower.empty() && lower.back() == 'x') {
    const std::string numeric = lower.substr(0, lower.size() - 1);
    if (!numeric.empty()) {
      return sanitizeAntiAliasingSamples(std::atoi(numeric.c_str()));
    }
  }

  return ClientDefaults::kDefaultAntiAliasingSamples;
}

#ifdef _WIN32
std::optional<glm::ivec2> currentDisplayResolution() {
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
    return std::nullopt;
  }

  const glm::ivec2 resolution(static_cast<int>(mode.dmPelsWidth),
                              static_cast<int>(mode.dmPelsHeight));
  if (!isValidResolution(resolution)) {
    return std::nullopt;
  }

  return resolution;
}
#endif


glm::ivec2 defaultResolutionFallback(
    const std::vector<glm::ivec2>& availableResolutions) {
  if (!availableResolutions.empty()) {
#ifdef _WIN32
    if (const std::optional<glm::ivec2> currentResolution =
            currentDisplayResolution();
        currentResolution && hasResolution(availableResolutions, *currentResolution)) {
      return *currentResolution;
    }
#endif

    constexpr glm::ivec2 kLegacyDefaultResolution =
        ClientDefaults::kDefaultWindowedResolution;
    if (hasResolution(availableResolutions, kLegacyDefaultResolution)) {
      return kLegacyDefaultResolution;
    }
    return availableResolutions.front();
  }

  return ClientDefaults::kDefaultWindowedResolution;
}

} // namespace


#pragma endregion

#pragma region SettingsSerialization
std::string GameSettings::fontAssetPath() const {
  return (std::filesystem::path(ClientAssets::kFontsDirectory) / fontFile)
      .generic_string();
}


std::string GameSettings::fontDisplayName() const {
  return std::filesystem::path(fontFile).stem().string();
}

// Funcao: normaliza 'sanitize' na configuracao persistente do runtime.
// Detalhe: usa 'availableFonts', 'availableResolutions' para limpar a entrada para reduzir inconsistencias antes do uso.
void GameSettings::sanitize(
    const std::vector<std::string>& availableFonts,
    const std::vector<glm::ivec2>& availableResolutions) {
  // O contrato aqui e defensivo: qualquer arquivo antigo ou corrompido volta para um estado valido.
  mouseSensitivity = std::clamp(mouseSensitivity,
                                ClientDefaults::kMinMouseSensitivity,
                                ClientDefaults::kMaxMouseSensitivity);
  fieldOfView = std::clamp(fieldOfView,
                           ClientDefaults::kMinFieldOfView,
                           ClientDefaults::kMaxFieldOfView);
  renderScale = std::clamp(renderScale, ClientDefaults::kMinRenderScale,
                           ClientDefaults::kMaxRenderScale);
  antiAliasingSamples = sanitizeAntiAliasingSamples(antiAliasingSamples);
  audioSettings.sanitize();
  fontFile = sanitizeFontFileName(fontFile);
  if (!isValidResolution(resolution) ||
      (!availableResolutions.empty() &&
       !hasResolution(availableResolutions, resolution))) {
    resolution = defaultResolutionFallback(availableResolutions);
  }

  if (windowMode == ENGINE::VIEWPORTMODE::BORDERLESS) {
    // Borderless should always track the active monitor resolution.
    resolution = defaultResolutionFallback(availableResolutions);
  }

  if (!availableFonts.empty()) {
    auto matchesFont = [&](const std::string& candidate) {
      return toLower(candidate) == toLower(fontFile);
    };
    const auto fontIt =
        std::find_if(availableFonts.begin(), availableFonts.end(), matchesFont);
    if (fontIt != availableFonts.end()) {
      fontFile = *fontIt;
    } else {
      const auto defaultIt =
          std::find_if(availableFonts.begin(), availableFonts.end(),
                       [](const std::string& candidate) {
                         return toLower(candidate) ==
                                toLower(ClientDefaults::kDefaultFontFile);
                       });
      fontFile = defaultIt != availableFonts.end() ? *defaultIt : availableFonts.front();
    }
  } else if (fontFile.empty()) {
    fontFile = ClientDefaults::kDefaultFontFile;
  }
}

bool GameSettings::save(std::string* outError) const {
  std::error_code ec;
  const std::filesystem::path directory = settingsDirectory();
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    if (outError) {
      *outError = "Failed to create settings directory: " + directory.string();
    }
    return false;
  }

  nlohmann::json json;
  json["renderDistance"] = renderDistanceToken(renderDistance);
  json["fontFile"] = sanitizeFontFileName(fontFile);
  json["resolution"] = {
      {"width", resolution.x},
      {"height", resolution.y},
  };
  json["mouseSensitivity"] =
      std::round(static_cast<double>(mouseSensitivity) * 1000000.0) / 1000000.0;
  json["fieldOfView"] =
      std::round(static_cast<double>(fieldOfView) * 10.0) / 10.0;
  json["renderScale"] =
      std::round(static_cast<double>(renderScale) * 1000.0) / 1000.0;
  json["antiAliasingSamples"] = sanitizeAntiAliasingSamples(antiAliasingSamples);
  json["windowMode"] = windowModeToken(windowMode);
  json["vSyncEnabled"] = vSyncEnabled;
  json["showFpsCounterOnly"] = showFpsCounterOnly;
  json["advancedLightingEnabled"] = advancedLightingEnabled;
  json["cloudsEnabled"] = cloudsEnabled;
  json["cloudQuality"] = VoxelGame::Clouds::cloudQualityId(cloudQuality);
  nlohmann::json categoryVolumes = nlohmann::json::object();
  nlohmann::json categoryMuted = nlohmann::json::object();
  for (ENGINE::AUDIO::SoundCategoryId category :
       ENGINE::AUDIO::kSoundCategoryOrder) {
    const char* categoryId = ENGINE::AUDIO::soundCategoryIdToString(category);
    categoryVolumes[categoryId] = audioSettings.volumeFor(category);
    categoryMuted[categoryId] = audioSettings.categoryMuted[ENGINE::AUDIO::toIndex(category)];
  }
  json["audio"] = {
      {"musicEnabled", audioSettings.musicEnabled},
      {"globalMute", audioSettings.globalMute},
      {"categoryVolumes", categoryVolumes},
      {"categoryMuted", categoryMuted},
  };
  nlohmann::json controlsJson = nlohmann::json::object();
  for (const auto& [actionId, binding] : controlOverrides) {
    if (binding.empty()) {
      continue;
    }

    controlsJson[actionId] = {
        {"inputType", InputMapping::inputBindingTypeToken(binding.type)},
        {"bind", binding.codeName},
    };
  }
  json["controls"] = controlsJson;

  std::ofstream output(settingsFilePath(), std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    if (outError) {
      *outError = "Failed to open settings file for writing: " +
                  settingsFilePath().string();
    }
    return false;
  }

  output << json.dump(2);
  if (!output) {
    if (outError) {
      *outError = "Failed to write settings file: " + settingsFilePath().string();
    }
    return false;
  }

  return true;
}

GameSettings GameSettings::load(std::string* outError) {
  GameSettings settings;
  const std::vector<std::string> fonts = availableFonts();
  const std::vector<glm::ivec2> resolutions = availableDisplayResolutions();
  const std::filesystem::path path = settingsFilePath();

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    settings.sanitize(fonts, resolutions);
    return settings;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    if (outError) {
      *outError = "Failed to open settings file: " + path.string();
    }
    settings.sanitize(fonts, resolutions);
    return settings;
  }

  try {
    // O loader aceita integer e float para manter compatibilidade com arquivos mais antigos.
    nlohmann::json json = nlohmann::json::parse(input, nullptr, true, true);
    if (json.contains("renderDistance") && json["renderDistance"].is_string()) {
      settings.renderDistance =
          parseRenderDistanceToken(json["renderDistance"].get<std::string>());
    }
    if (json.contains("fontFile") && json["fontFile"].is_string()) {
      settings.fontFile = json["fontFile"].get<std::string>();
    }
    if (json.contains("resolution") && json["resolution"].is_object()) {
      const nlohmann::json& resolutionJson = json["resolution"];
      if (resolutionJson.contains("width") &&
          resolutionJson["width"].is_number_integer()) {
        settings.resolution.x = resolutionJson["width"].get<int>();
      }
      if (resolutionJson.contains("height") &&
          resolutionJson["height"].is_number_integer()) {
        settings.resolution.y = resolutionJson["height"].get<int>();
      }
    }
    if (json.contains("mouseSensitivity") &&
        json["mouseSensitivity"].is_number_float()) {
      settings.mouseSensitivity = json["mouseSensitivity"].get<float>();
    } else if (json.contains("mouseSensitivity") &&
               json["mouseSensitivity"].is_number_integer()) {
      settings.mouseSensitivity =
          static_cast<float>(json["mouseSensitivity"].get<int>());
    }
    if (json.contains("fieldOfView") && json["fieldOfView"].is_number()) {
      settings.fieldOfView = json["fieldOfView"].get<float>();
    }
    if (json.contains("renderScale") && json["renderScale"].is_number()) {
      settings.renderScale = json["renderScale"].get<float>();
    }
    if (json.contains("antiAliasingSamples") &&
        json["antiAliasingSamples"].is_number_integer()) {
      settings.antiAliasingSamples =
          sanitizeAntiAliasingSamples(json["antiAliasingSamples"].get<int>());
    } else if (json.contains("antiAliasingSamples") &&
               json["antiAliasingSamples"].is_string()) {
      settings.antiAliasingSamples = parseAntiAliasingSamplesToken(
          json["antiAliasingSamples"].get<std::string>());
    }
    if (json.contains("windowMode") && json["windowMode"].is_string()) {
      settings.windowMode =
          parseWindowModeToken(json["windowMode"].get<std::string>());
    }
    if (json.contains("vSyncEnabled") && json["vSyncEnabled"].is_boolean()) {
      settings.vSyncEnabled = json["vSyncEnabled"].get<bool>();
    }
    if (json.contains("showFpsCounterOnly") &&
        json["showFpsCounterOnly"].is_boolean()) {
      settings.showFpsCounterOnly = json["showFpsCounterOnly"].get<bool>();
    }
    if (json.contains("advancedLightingEnabled") &&
        json["advancedLightingEnabled"].is_boolean()) {
      settings.advancedLightingEnabled = json["advancedLightingEnabled"].get<bool>();
    }
    if (json.contains("cloudsEnabled") && json["cloudsEnabled"].is_boolean()) {
      settings.cloudsEnabled = json["cloudsEnabled"].get<bool>();
    }
    if (json.contains("cloudQuality") && json["cloudQuality"].is_string()) {
      VoxelGame::Clouds::CloudQuality parsedQuality = settings.cloudQuality;
      if (VoxelGame::Clouds::tryParseCloudQuality(
              json["cloudQuality"].get<std::string>(), parsedQuality)) {
        settings.cloudQuality = parsedQuality;
      }
    }
    if (json.contains("audio") && json["audio"].is_object()) {
      const nlohmann::json& audioJson = json["audio"];
      if (audioJson.contains("musicEnabled") && audioJson["musicEnabled"].is_boolean()) {
        settings.audioSettings.musicEnabled = audioJson["musicEnabled"].get<bool>();
      }
      if (audioJson.contains("globalMute") && audioJson["globalMute"].is_boolean()) {
        settings.audioSettings.globalMute = audioJson["globalMute"].get<bool>();
      }
      if (audioJson.contains("categoryVolumes") &&
          audioJson["categoryVolumes"].is_object()) {
        const nlohmann::json& volumesJson = audioJson["categoryVolumes"];
        for (ENGINE::AUDIO::SoundCategoryId category :
             ENGINE::AUDIO::kSoundCategoryOrder) {
          const char* categoryId = ENGINE::AUDIO::soundCategoryIdToString(category);
          if (volumesJson.contains(categoryId) &&
              volumesJson[categoryId].is_number()) {
            settings.audioSettings.categoryVolumes[ENGINE::AUDIO::toIndex(category)] =
                volumesJson[categoryId].get<float>();
          }
        }
      }
      if (audioJson.contains("categoryMuted") &&
          audioJson["categoryMuted"].is_object()) {
        const nlohmann::json& mutedJson = audioJson["categoryMuted"];
        for (ENGINE::AUDIO::SoundCategoryId category :
             ENGINE::AUDIO::kSoundCategoryOrder) {
          const char* categoryId = ENGINE::AUDIO::soundCategoryIdToString(category);
          if (mutedJson.contains(categoryId) &&
              mutedJson[categoryId].is_boolean()) {
            settings.audioSettings.categoryMuted[ENGINE::AUDIO::toIndex(category)] =
                mutedJson[categoryId].get<bool>();
          }
        }
      }
    }
    if (json.contains("controls") && json["controls"].is_object()) {
      const nlohmann::json& controlsJson = json["controls"];
      for (auto it = controlsJson.begin(); it != controlsJson.end(); ++it) {
        if (!it.value().is_object()) {
          continue;
        }

        const nlohmann::json& bindingJson = it.value();
        if (!bindingJson.contains("inputType") ||
            !bindingJson["inputType"].is_string() ||
            !bindingJson.contains("bind") ||
            !bindingJson["bind"].is_string()) {
          continue;
        }

        InputMapping::InputBinding binding;
        binding.type = InputMapping::parseInputBindingTypeToken(
            bindingJson["inputType"].get<std::string>());
        binding.codeName = bindingJson["bind"].get<std::string>();
        if (binding.empty()) {
          continue;
        }

        settings.controlOverrides[it.key()] = std::move(binding);
      }
    }
  } catch (const std::exception& exception) {
    if (outError) {
      *outError = std::string("Failed to parse settings file: ") + exception.what();
    }
  }

  settings.sanitize(fonts, resolutions);
  return settings;
}

#pragma endregion

#pragma region SettingsDiscovery
std::vector<std::string> GameSettings::availableFonts() {
  std::vector<std::string> fonts;
  std::error_code ec;
  const std::filesystem::path root = AppPaths::fontsRoot();
  if (!std::filesystem::exists(root, ec) ||
      !std::filesystem::is_directory(root, ec)) {
    return fonts;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    if (!isTtfFile(entry.path())) {
      continue;
    }
    fonts.push_back(entry.path().filename().string());
  }

  std::sort(fonts.begin(), fonts.end(), [](const std::string& a,
                                           const std::string& b) {
    return toLower(a) < toLower(b);
  });
  return fonts;
}

std::vector<glm::ivec2> GameSettings::availableDisplayResolutions() {
  std::vector<glm::ivec2> resolutions;

#ifdef _WIN32
  for (DWORD modeIndex = 0;; ++modeIndex) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, modeIndex, &mode)) {
      break;
    }

    const glm::ivec2 resolution(static_cast<int>(mode.dmPelsWidth),
                                static_cast<int>(mode.dmPelsHeight));
    if (!isValidResolution(resolution)) {
      continue;
    }
    resolutions.push_back(resolution);
  }
#endif

  std::sort(resolutions.begin(), resolutions.end(),
            [](const glm::ivec2& a, const glm::ivec2& b) {
              if (a.x != b.x) {
                return a.x > b.x;
              }
              return a.y > b.y;
            });
  resolutions.erase(
      std::unique(resolutions.begin(), resolutions.end(),
                  [](const glm::ivec2& a, const glm::ivec2& b) {
                    return sameResolution(a, b);
                  }),
      resolutions.end());

  if (resolutions.empty()) {
    resolutions.push_back(defaultResolutionFallback({}));
  }

  return resolutions;
}

#pragma endregion

#pragma region SettingsPaths
std::filesystem::path GameSettings::settingsDirectory() {
#ifdef _WIN32
  if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
    if (localAppData[0] != '\0') {
      return std::filesystem::path(localAppData) / "VoxelParadoxData";
    }
  }
#endif
  return AppPaths::workspaceRoot() / "VoxelParadoxData";
}

std::filesystem::path GameSettings::settingsFilePath() {
  return settingsDirectory() / "GameSettings.json";
}
#pragma endregion
