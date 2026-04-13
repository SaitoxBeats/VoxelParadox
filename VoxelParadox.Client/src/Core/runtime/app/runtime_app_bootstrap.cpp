// runtime_app_bootstrap.cpp
// Bootstrap and validation steps that happen before the game loop starts.

#include "runtime/app/runtime_app_internal.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// External
#include <windows.h>

// Solution dependencies
#include "path/app_paths.hpp"

// Client
#include "client_defaults.hpp"
#include "input/input_action_system.hpp"
#include "runtime/ui/runtime_ui.hpp"
#include "world/persistence/world_save_service.hpp"
#include "world/biome/biome_registry.hpp"
#include "world/generation/chunk.hpp"
#include "world/generation/fractal_world.hpp"
#include "world/persistence/world_stack.hpp"

namespace {

namespace ConsoleColor {
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kLightBlue = "\x1b[96m";
constexpr const char* kGray = "\x1b[90m";
constexpr const char* kGreen = "\x1b[92m";
constexpr const char* kRed = "\x1b[91m";
}  // namespace ConsoleColor

bool hasBiomePresetExtension(const std::filesystem::path& path) {
  const std::string filename = path.filename().string();
  constexpr const char* kSuffix = ".fvbiome.json";
  constexpr std::size_t kSuffixLength = 13;
  return filename.size() >= kSuffixLength &&
         filename.compare(filename.size() - kSuffixLength, kSuffixLength, kSuffix) == 0;
}

void showStartupErrorPopup(const std::string& message) {
#ifdef _WIN32
  MessageBoxA(nullptr, message.c_str(), ClientDefaults::kWindowTitle,
              MB_OK | MB_ICONERROR);
#endif
  RuntimeAppInternal::printBootstrapError(message.c_str());
}

bool consoleColorsEnabled() {
  static const bool enabled = []() {
#ifdef _WIN32
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE || console == nullptr) {
      return false;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(console, &mode)) {
      return false;
    }

    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
      return true;
    }

    return SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
  }();
  return enabled;
}

void printColoredLine(const char* color, const char* format, ...) {
  if (consoleColorsEnabled()) {
    std::printf("%s", color);
  }

  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);

  if (consoleColorsEnabled()) {
    std::printf("%s", ConsoleColor::kReset);
  }
  std::fflush(stdout);
}

RuntimeAppInternal::RootWorldChunkCheck verifyRootWorldChunks(
    FractalWorld& world, const glm::vec3& spawnPosition, int radiusChunks = 1) {
  RuntimeAppInternal::RootWorldChunkCheck result;
  result.radiusChunks = radiusChunks;

  const int side = (radiusChunks * 2) + 1;
  result.expectedChunks = side * side * side;

  const glm::ivec3 center =
      FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(spawnPosition)));

  for (int x = -radiusChunks; x <= radiusChunks; x++) {
    for (int y = -radiusChunks; y <= radiusChunks; y++) {
      for (int z = -radiusChunks; z <= radiusChunks; z++) {
        Chunk* chunk = world.getChunkBlocking(center + glm::ivec3(x, y, z));
        if (!chunk) {
          continue;
        }

        result.loadedChunks++;
        if (chunk->generated.load()) {
          result.generatedChunks++;
        }
      }
    }
  }

  result.passed = result.loadedChunks == result.expectedChunks &&
                  result.generatedChunks == result.expectedChunks &&
                  world.isAreaGenerated(spawnPosition, radiusChunks);
  return result;
}

}  // namespace

namespace RuntimeAppInternal {

RuntimeSettingsBundle loadRuntimeSettings(std::string* outLoadError) {
  RuntimeSettingsBundle bundle;
  bundle.applied = GameSettings::load(outLoadError);
  bundle.availableFonts = GameSettings::availableFonts();
  bundle.availableResolutions = GameSettings::availableDisplayResolutions();
  bundle.applied.sanitize(bundle.availableFonts, bundle.availableResolutions);
  std::string controlsStatus;
  InputMapping::InputActionSystem::instance().initialize(&controlsStatus);
  InputMapping::InputActionSystem::instance().sanitizeOverrides(
      bundle.applied.controlOverrides);
  InputMapping::InputActionSystem::instance().applyOverrides(
      bundle.applied.controlOverrides);
  bundle.pending = bundle.applied;
  RuntimeUI::saveGameSettings(bundle.applied);
  bundle.uiState.lastNonFullscreenMode =
      bundle.applied.windowMode == ENGINE::VIEWPORTMODE::FULLSCREEN
          ? ENGINE::VIEWPORTMODE::BORDERLESS
          : bundle.applied.windowMode;
  return bundle;
}

void printBootstrapInfo(const char* message) {
  printColoredLine(ConsoleColor::kLightBlue, "[INFO] %s\n", message);
}

void printBootstrapSuccess(const char* message) {
  printColoredLine(ConsoleColor::kGreen, "[Bootstrap : SUCCESS] %s\n\n", message);
}

void printBootstrapError(const char* message) {
  printColoredLine(ConsoleColor::kRed, "[Bootstrap : ERROR] %s\n", message);
}

void printBootstrapDetail(const char* label, const std::string& value) {
  printColoredLine(ConsoleColor::kGray, "%s %s\n", label, value.c_str());
}

bool loadRequiredBiomePresets(
    BiomeSelection& outRootSelection,
    std::shared_ptr<const VoxelGame::BiomePreset>& outRootPreset) {
  // --- 1. Validate Preset Directory ---
  const std::filesystem::path presetDirectory = AppPaths::biomePresetsRoot();
  std::error_code errorCode;

  if (!std::filesystem::exists(presetDirectory, errorCode) ||
      !std::filesystem::is_directory(presetDirectory, errorCode)) {
    showStartupErrorPopup("Voxel Paradox requires at least one biome preset.\n\n"
                          "The folder Resources/World/BiomesPresets was not found.");
    return false;
  }

  // --- 2. Discover Candidate Presets ---
  std::vector<std::filesystem::path> presetFiles;
  for (const auto& entry : std::filesystem::directory_iterator(presetDirectory, errorCode)) {
    if (errorCode) {
      break;
    }
    if (!entry.is_regular_file() || !hasBiomePresetExtension(entry.path())) {
      continue;
    }
    presetFiles.push_back(entry.path());
  }

  if (presetFiles.empty()) {
    showStartupErrorPopup("Voxel Paradox requires at least one biome preset to start.\n\n"
                          "The folder Resources/World/BiomesPresets is empty.");
    return false;
  }

  // --- 3. Load Registry And Pick Root Biome ---
  if (!BiomeRegistry::instance().reload(presetDirectory) ||
      BiomeRegistry::instance().presets().empty()) {
    showStartupErrorPopup("Voxel Paradox found biome preset files, but none of them "
                          "could be loaded.\n\n"
                          "Check Resources/World/BiomesPresets for invalid .fvbiome.json files.");
    return false;
  }

  const auto& presets = BiomeRegistry::instance().presets();
  auto rootPresetIt = std::find_if(
      presets.begin(), presets.end(),
      [](const BiomeRegistry::PresetEntry& entry) {
        return entry.selection.presetId ==
               ClientDefaults::kPreferredRootBiomePresetId;
      });
  if (rootPresetIt == presets.end()) {
    rootPresetIt = presets.begin();
  }

  const BiomeRegistry::PresetEntry& rootPreset = *rootPresetIt;
  outRootSelection = rootPreset.selection;
  outRootPreset = rootPreset.preset;
  return true;
}

Bootstrap::Config makeBootstrapConfig(const GameSettings& settings) {
  Bootstrap::Config config;
  config.openGLMajor = ClientDefaults::kOpenGLMajor;
  config.openGLMinor = ClientDefaults::kOpenGLMinor;
  config.windowTitle = ClientDefaults::kWindowTitle;
  config.windowSize = RuntimeUI::appliedViewportSizeForSettings(settings);
  config.viewportMode = settings.windowMode;
  config.defaultFontPath = settings.fontAssetPath();
  config.vSyncEnabled = settings.vSyncEnabled;
  return config;
}

bool prepareWorldFromSession(
    WorldStack& worldStack, const WorldSaveService::WorldSession& session,
    const glm::vec3& fallbackSpawnAnchor, float playerRadius, float standingHeight,
    float eyeHeight, WorldStack::RenderDistancePreset renderDistancePreset,
    glm::vec3& outResolvedSpawnPosition) {
  // --- 1. Create Selected World ---
  printBootstrapInfo("Preparing the selected world...");
  WorldStack::setSaveWorldDirectory(session.paths.universesDirectory.string());
  worldStack.init(session.manifest.rootSeed, session.manifest.rootBiomeSelection,
                  session.rootPreset, session.manifest.rootDepth);
  worldStack.setRenderDistancePreset(renderDistancePreset);

  FractalWorld* world = worldStack.currentWorld();
  if (!world) {
    printBootstrapError("Failed to create the world.");
    return false;
  }

  if (!session.playerData.traversalStack.empty()) {
    if (!worldStack.restoreTraversalStack(session.playerData.traversalStack)) {
      printBootstrapError("Failed to restore the saved world traversal stack.");
      return false;
    }
    world = worldStack.currentWorld();
    if (!world) {
      printBootstrapError("Failed to resolve the active world after restore.");
      return false;
    }
  }

  if (session.hasPlayerData) {
    outResolvedSpawnPosition = session.playerData.playerState.cameraPosition;
  } else {
    outResolvedSpawnPosition = worldStack.resolveCurrentWorldSpawnPosition(
        fallbackSpawnAnchor, playerRadius, standingHeight, eyeHeight);
  }

  world->primeImmediateArea(outResolvedSpawnPosition, 1);

  // --- 2. Emit Bootstrap Diagnostics ---
  printBootstrapDetail("World Name:", session.manifest.displayName);
  printBootstrapDetail("World Seed:", std::to_string(world->seed));
  printBootstrapDetail("World Biome:", worldStack.currentBiomeName());
  printBootstrapDetail("World Depth:", std::to_string(worldStack.currentDepth()));
  printBootstrapDetail("World Render Distance:",
                       std::to_string(world->renderDistance));

  // --- 3. Validate Spawn Chunk Shell ---
  const RootWorldChunkCheck chunkCheck =
      verifyRootWorldChunks(*world, outResolvedSpawnPosition, 1);
  printBootstrapDetail("Spawn Chunk Radius:",
                       std::to_string(chunkCheck.radiusChunks));
  printBootstrapDetail("Spawn Chunks Loaded:",
                       std::to_string(chunkCheck.loadedChunks) + "/" +
                           std::to_string(chunkCheck.expectedChunks));
  printBootstrapDetail("Spawn Chunks Generated:",
                       std::to_string(chunkCheck.generatedChunks) + "/" +
                           std::to_string(chunkCheck.expectedChunks));
  printBootstrapDetail("Chunk Generation Check:",
                       chunkCheck.passed ? "OK" : "FAILED");

  if (!chunkCheck.passed) {
    printBootstrapError("World chunk generation check failed.");
    return false;
  }

  printBootstrapSuccess("World initialized!");
  return true;
}

}  // namespace RuntimeAppInternal
