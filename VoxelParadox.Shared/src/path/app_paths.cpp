// Arquivo: Engine/src/path/app_paths.cpp
// Papel: implementa um componente compartilhado do engine.
// Fluxo: Ã© reutilizado pelos alvos do jogo e do editor.
// DependÃªncias principais: headers e bibliotecas compartilhadas do projeto.
#include "app_paths.hpp"

// Arquivo: Engine/src/path/app_paths.cpp
// Papel: implementa a descoberta do workspace e a traduÃƒÂ§ÃƒÂ£o de aliases legados de recursos.
// Fluxo: parte do executÃƒÂ¡vel atual, sobe pela ÃƒÂ¡rvore de diretÃƒÂ³rios e fixa as raÃƒÂ­zes de
// `workspaceRoot`, `resourcesRoot` e `engineRoot`, usadas por todo o projeto.
// DependÃƒÂªncias principais: Win32 para localizar o executÃƒÂ¡vel e `std::filesystem`.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace AppPaths {
namespace {

struct PathState {
  bool initialized = false;
  std::filesystem::path executablePath{};
  std::filesystem::path executableDirectory{};
  std::filesystem::path workspaceRoot{};
  std::filesystem::path resourcesRoot{};
  std::filesystem::path engineRoot{};
};

PathState g_state;

std::filesystem::path detectEngineRoot(const std::filesystem::path& candidateRoot) {
  std::error_code ec;
  constexpr std::array<const char*, 2> kEngineFolderCandidates = {
      "VoxelParadox.Engine",
      "Engine",
  };

  for (const char* folderName : kEngineFolderCandidates) {
    const std::filesystem::path candidate = candidateRoot / folderName;
    if (std::filesystem::exists(candidate, ec) &&
        std::filesystem::is_directory(candidate, ec)) {
      return candidate;
    }
    ec.clear();
  }

  return candidateRoot / kEngineFolderCandidates.front();
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::filesystem::path tryResolveProcessExecutablePath() {
#ifdef _WIN32
  std::array<char, MAX_PATH> buffer{};
  DWORD length = GetModuleFileNameA(nullptr, buffer.data(),
                                    static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return {};
  }
  return std::filesystem::path(std::string(buffer.data(), length));
#else
  return {};
#endif
}

std::vector<std::filesystem::path> candidateRootsFor(
    const std::filesystem::path& executableDirectory) {
  std::vector<std::filesystem::path> roots;
  std::error_code ec;

  auto appendParents = [&](std::filesystem::path start) {
    if (start.empty()) {
      return;
    }
    start = std::filesystem::weakly_canonical(start, ec);
    ec.clear();
    while (!start.empty()) {
      roots.push_back(start);
      const std::filesystem::path parent = start.parent_path();
      if (parent == start) {
        break;
      }
      start = parent;
    }
  };

  appendParents(executableDirectory);
  appendParents(std::filesystem::current_path(ec));
  return roots;
}

bool tryBindWorkspaceRoot(const std::filesystem::path& candidateRoot,
                          PathState& state) {
  // O projeto suporta dois layouts: desenvolvimento (`res/`) e pacote staged (`Resources/`).
  std::error_code ec;
  const std::filesystem::path packagedResources = candidateRoot / "Resources";
  if (std::filesystem::exists(packagedResources, ec) &&
      std::filesystem::is_directory(packagedResources, ec)) {
    state.workspaceRoot = candidateRoot;
    state.resourcesRoot = packagedResources;
    state.engineRoot = detectEngineRoot(candidateRoot);
    return true;
  }

  ec.clear();
  const std::filesystem::path devResources = candidateRoot / "res";
  if (std::filesystem::exists(devResources, ec) &&
      std::filesystem::is_directory(devResources, ec)) {
    state.workspaceRoot = candidateRoot;
    state.resourcesRoot = devResources;
    state.engineRoot = detectEngineRoot(candidateRoot);
    return true;
  }

  return false;
}

std::filesystem::path mapLegacyAssetPath(const std::filesystem::path& relativePath) {
  if (relativePath.empty()) {
    return relativePath;
  }

  auto it = relativePath.begin();
  ++it;

  if (it == relativePath.end()) {
    return assetsRoot();
  }

  const std::string category = lowercase(it->generic_string());
  ++it;

  std::filesystem::path result;
  if (category == "hud") {
    result = hudTexturesRoot();
  } else if (category == "items") {
    result = itemTexturesRoot();
  } else if (category == "recipes") {
    result = recipesRoot();
  } else if (category == "shaders") {
    result = shadersRoot();
  } else if (category == "voxs") {
    result = voxsRoot();
  } else {
    result = assetsRoot() / category;
  }

  for (; it != relativePath.end(); ++it) {
    result /= *it;
  }
  return result;
}

std::filesystem::path mapLegacySavePath(const std::filesystem::path& relativePath) {
  if (relativePath.empty()) {
    return savesRoot();
  }

  auto it = relativePath.begin();
  ++it;

  if (it == relativePath.end()) {
    return savesRoot();
  }

  const std::string category = lowercase(it->generic_string());
  ++it;

  std::filesystem::path result;
  if (category == "world") {
    result = saveWorldsRoot();
  } else if (category == "biomemaker") {
    result = biomeMakerSavesRoot();
  } else if (category == "seeds") {
    result = seedsRoot();
  } else {
    result = savesRoot() / category;
  }

  for (; it != relativePath.end(); ++it) {
    result /= *it;
  }
  return result;
}

} // namespace

bool initialize() {
  if (g_state.initialized) {
    return true;
  }
  return initializeFromExecutable(tryResolveProcessExecutablePath());
}

bool initializeFromExecutable(const std::filesystem::path& executablePath) {
  if (g_state.initialized) {
    return true;
  }

  std::error_code ec;
  PathState state;

  state.executablePath = executablePath.empty()
                             ? tryResolveProcessExecutablePath()
                             : executablePath;
  if (state.executablePath.empty()) {
    state.executablePath = std::filesystem::current_path(ec) / "unknown.exe";
  }

  state.executablePath = std::filesystem::weakly_canonical(state.executablePath, ec);
  if (ec) {
    ec.clear();
    state.executablePath = std::filesystem::absolute(state.executablePath, ec);
  }
  state.executableDirectory = state.executablePath.parent_path();

  const std::vector<std::filesystem::path> candidates =
      candidateRootsFor(state.executableDirectory);
  for (const std::filesystem::path& candidate : candidates) {
    if (tryBindWorkspaceRoot(candidate, state)) {
      state.initialized = true;
      g_state = state;
      return true;
    }
  }

  state.workspaceRoot = state.executableDirectory;
  state.resourcesRoot = state.workspaceRoot / "Resources";
  state.engineRoot = detectEngineRoot(state.workspaceRoot);
  state.initialized = true;
  g_state = state;
  return false;
}

const std::filesystem::path& executablePath() {
  initialize();
  return g_state.executablePath;
}

const std::filesystem::path& executableDirectory() {
  initialize();
  return g_state.executableDirectory;
}

const std::filesystem::path& workspaceRoot() {
  initialize();
  return g_state.workspaceRoot;
}

const std::filesystem::path& resourcesRoot() {
  initialize();
  return g_state.resourcesRoot;
}

const std::filesystem::path& engineRoot() {
  initialize();
  return g_state.engineRoot;
}

std::filesystem::path gameDataRoot() {
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        if (localAppData[0] != '\0') {
            return std::filesystem::path(localAppData) / "VoxelParadoxData";
		}
    }
#endif // _WIN32
	return AppPaths::workspaceRoot() / "VoxelParadoxData";
}

std::filesystem::path sharedRoot() {
  return resourcesRoot() / "Shared";
}

std::filesystem::path assetsRoot() {
  return resourcesRoot() / "Assets";
}

std::filesystem::path fontsRoot() {
  return assetsRoot() / "Fonts";
}

std::filesystem::path texturesRoot() {
  return assetsRoot() / "Textures";
}

std::filesystem::path hudTexturesRoot() {
  return texturesRoot() / "Hud";
}

std::filesystem::path itemTexturesRoot() {
  return texturesRoot() / "Items";
}

std::filesystem::path recipesRoot() {
  return assetsRoot() / "Recipes";
}

std::filesystem::path shadersRoot() {
  return assetsRoot() / "Shaders";
}

std::filesystem::path voxsRoot() {
  return assetsRoot() / "Voxs";
}

std::filesystem::path savesRoot() {
  return gameDataRoot() / "Saves";
}

std::filesystem::path saveWorldsRoot() {
  return savesRoot() / "worlds";
}

std::filesystem::path biomeMakerSavesRoot() {
  return savesRoot() / "BiomeMaker";
}

std::filesystem::path seedsRoot() {
  return savesRoot() / "Seeds";
}

std::filesystem::path worldRoot() {
  return resourcesRoot() / "World";
}

std::filesystem::path biomePresetsRoot() {
  return worldRoot() / "BiomesPresets";
}

std::filesystem::path modulesRoot() {
  return worldRoot() / "Modules";
}

std::filesystem::path workspaceFile(const std::filesystem::path& relativePath) {
  return workspaceRoot() / relativePath;
}

std::filesystem::path resolve(const std::filesystem::path& path) {
  initialize();

  if (path.empty() || path.is_absolute()) {
    return path;
  }

  const std::string lower = lowercase(path.generic_string());
  // Esses aliases preservam compatibilidade com caminhos histÃƒÂ³ricos espalhados pelo runtime.
  if (lower == "assets" || lower.rfind("assets/", 0) == 0) {
    return mapLegacyAssetPath(path);
  }
  if (lower == "world" || lower.rfind("world/", 0) == 0) {
    std::filesystem::path relative("World");
    auto it = path.begin();
    ++it;
    for (; it != path.end(); ++it) {
      relative /= *it;
    }
    return resourcesRoot() / relative;
  }
  if (lower == "presets" || lower.rfind("presets/", 0) == 0) {
    if (lower == "presets/modules" || lower.rfind("presets/modules/", 0) == 0) {
      std::filesystem::path relative;
      bool skippedModules = false;
      for (const auto& part : path) {
        const std::string segment = lowercase(part.generic_string());
        if (segment == "presets") {
          continue;
        }
        if (!skippedModules && segment == "modules") {
          skippedModules = true;
          continue;
        }
        relative /= part;
      }
      return modulesRoot() / relative;
    }

    std::filesystem::path relative;
    bool skippedPresets = false;
    for (const auto& part : path) {
      const std::string segment = lowercase(part.generic_string());
      if (!skippedPresets && segment == "presets") {
        skippedPresets = true;
        continue;
      }
      relative /= part;
    }
    return biomePresetsRoot() / relative;
  }
  if (lower == "saves" || lower.rfind("saves/", 0) == 0) {
    return mapLegacySavePath(path);
  }
  if (lower == "engine" || lower.rfind("engine/", 0) == 0) {
    std::filesystem::path relative;
    auto it = path.begin();
    ++it;
    for (; it != path.end(); ++it) {
      relative /= *it;
    }
    return engineRoot() / relative;
  }
  if (lower == "res" || lower.rfind("res/", 0) == 0) {
    std::filesystem::path relative;
    auto it = path.begin();
    ++it;
    for (; it != path.end(); ++it) {
      relative /= *it;
    }
    return resourcesRoot() / relative;
  }
  if (lower == "resources" || lower.rfind("resources/", 0) == 0) {
    std::filesystem::path relative;
    auto it = path.begin();
    ++it;
    for (; it != path.end(); ++it) {
      relative /= *it;
    }
    return resourcesRoot() / relative;
  }

  return workspaceRoot() / path;
}

std::string resolveString(const std::filesystem::path& path) {
  return resolve(path).string();
}

} // namespace AppPaths
