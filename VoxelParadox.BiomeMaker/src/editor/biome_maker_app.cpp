#include "biome_maker_app.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"
#include "ui/imgui_layer.hpp"
#include "world/chunk_generator_source.hpp"

namespace BiomeMaker {
namespace {

constexpr std::uint32_t kDefaultSeed = 42;
constexpr const char* kWindowTitle = "VoxelParadox BiomeMaker";
constexpr const char* kDefaultFontPath = "Assets/Fonts/ChillBitmap.ttf";

std::filesystem::path presetRootDirectory() {
  return AppPaths::biomePresetsRoot();
}

std::filesystem::path moduleLibraryDirectory() {
  return AppPaths::modulesRoot();
}

std::filesystem::path defaultSharedModulePath(const char* fileName) {
  return (std::filesystem::path("..") / "Modules" / fileName).lexically_normal();
}

std::filesystem::path defaultPrivateModuleDirectory(const std::string& presetId) {
  return std::filesystem::path(sanitizePresetId(presetId) + ".modules")
      .lexically_normal();
}

std::filesystem::path editorIniPath() {
  return AppPaths::workspaceFile("imgui_biomaker.ini");
}

bool fileContainsDockingData(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line == "[Docking][Data]") {
      return true;
    }
  }
  return false;
}

template <std::size_t N>
void copyStringToBuffer(char (&buffer)[N], const std::string& value) {
  std::snprintf(buffer, N, "%s", value.c_str());
}

std::string simplifyModuleName(const std::string& value, ModuleType type) {
  std::string simplified = value.empty() ? moduleTypeDisplayName(type) : value;
  const auto replaceAll = [&](const char* from, const char* to) {
    const std::string needle(from);
    const std::string replacement(to);
    std::size_t position = 0;
    while ((position = simplified.find(needle, position)) != std::string::npos) {
      simplified.replace(position, needle.size(), replacement);
      position += replacement.size();
    }
  };

  replaceAll("Perlin Terrain", "Perlin");
  replaceAll("Import Vox Files", "Vox Import");
  return simplified;
}

std::string displayModuleName(const BiomeModule& module) {
  return simplifyModuleName(module.displayName, module.type);
}

std::string displayModuleFileName(const std::filesystem::path& path) {
  if (path.empty()) {
    return "unsaved";
  }
  std::string fileName = path.filename().string();
  constexpr std::string_view suffix = ".fvmodule.json";
  if (fileName.size() > suffix.size() &&
      fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) == 0) {
    fileName.erase(fileName.size() - suffix.size());
  }
  return fileName;
}

void clearMainFramebuffer(GLFWwindow* window) {
  if (!window) {
    return;
  }

  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  if (framebufferWidth <= 0 || framebufferHeight <= 0) {
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, framebufferWidth, framebufferHeight);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

} // namespace

bool BiomeMakerApp::run() {
  if (!initialize()) {
    shutdown();
    return false;
  }

  double lastTimeSeconds = glfwGetTime();
  ENGINE::INIT(lastTimeSeconds);

  while (!glfwWindowShouldClose(window_)) {
    const double currentTimeSeconds = glfwGetTime();
    float dtSeconds = static_cast<float>(currentTimeSeconds - lastTimeSeconds);
    lastTimeSeconds = currentTimeSeconds;
    dtSeconds = glm::min(dtSeconds, 0.05f);

    ENGINE::UPDATE(currentTimeSeconds, dtSeconds);
    ENGINE::BEGINPERFFRAME();
    const auto cpuFrameStart = std::chrono::steady_clock::now();
    Input::update();

    ImGuiLayer::beginFrame();
    handleShortcuts();
    drawUi(dtSeconds, static_cast<float>(currentTimeSeconds));
    clearMainFramebuffer(window_);
    ImGuiLayer::render();

    const auto cpuFrameEnd = std::chrono::steady_clock::now();
    const float cpuFrameMs =
        std::chrono::duration<float, std::milli>(cpuFrameEnd - cpuFrameStart).count();
    const float fpsInstant = dtSeconds > 0.0f ? (1.0f / dtSeconds) : 0.0f;
    ENGINE::ENDPERFFRAME(cpuFrameMs, fpsInstant);

    glfwSwapBuffers(window_);
  }

  shutdown();
  return true;
}

bool BiomeMakerApp::initialize() {
  ensureWorkspaceFolders();
  ensureDefaultModuleLibrary();

  bootstrapConfig_.windowTitle = kWindowTitle;
  bootstrapConfig_.windowSize = glm::ivec2(1600, 900);
  bootstrapConfig_.viewportMode = ENGINE::VIEWPORTMODE::WINDOWMODE;
  bootstrapConfig_.defaultFontPath = kDefaultFontPath;
  bootstrapConfig_.saveDirectory = "Saves/BiomeMaker";

  if (!Bootstrap::initialize(bootstrapConfig_, window_)) {
    appendLog("Bootstrap initialization failed.");
    return false;
  }

  glfwMaximizeWindow(window_);

  if (!renderer_.init()) {
    appendLog("Editor renderer initialization failed.");
    return false;
  }

  ImGuiLayer::Config imguiConfig;
  imguiConfig.dockingEnabled = true;
  imguiConfig.iniFilename = editorIniPath().string();
  const bool imguiInitialized = ImGuiLayer::initialize(window_, imguiConfig);
  Bootstrap::reportImGuiStatus(imguiInitialized);
  if (!imguiInitialized) {
    appendLog("ImGui initialization failed.");
    return false;
  }
  dockLayoutInitialized_ = fileContainsDockingData(editorIniPath());

  Input::setCursorVisible(true);
  previewWorld_.setAnchorChunk(glm::ivec3(0));
  camera_.farPlane = 600.0f;
  cameraController_.focusOnAnchor(camera_, previewWorld_.anchorChunk());

  scanModuleLibrary();
  scanPresetFiles();
  ensureDefaultPresetExists();
  scanModuleLibrary();
  scanPresetFiles();
  if (!presetFiles_.empty()) {
    loadPresetDocument(presetFiles_.front());
  } else {
    newPresetDocument();
  }
  syncPreviewSource();
  appendLog("BiomeMaker initialized.");
  return true;
}

void BiomeMakerApp::shutdown() {
  previewWorld_.clear();
  renderer_.cleanup();
  ImGuiLayer::shutdown();
  ENGINE::CLEANUPPERF();
  Bootstrap::shutdownWindow(window_);
  window_ = nullptr;
}

void BiomeMakerApp::ensureWorkspaceFolders() {
  std::error_code ec;
  std::filesystem::create_directories(presetRootDirectory(), ec);
  ec.clear();
  std::filesystem::create_directories(moduleLibraryDirectory(), ec);
  ec.clear();
  std::filesystem::create_directories(AppPaths::biomeMakerSavesRoot(), ec);
}

void BiomeMakerApp::ensureDefaultModuleLibrary() {
  struct DefaultModuleSeed {
    const char* id;
    ModuleType type;
  };

  const DefaultModuleSeed defaults[] = {
      {"perlin", ModuleType::PERLIN_TERRAIN},
      {"vox_import", ModuleType::IMPORT_VOX_FILES},
      {"grid_pattern", ModuleType::GRID_PATTERN},
      {"menger_sponge", ModuleType::MENGER_SPONGE},
      {"cave_system", ModuleType::CAVE_SYSTEM},
      {"cellular_noise", ModuleType::CELLULAR_NOISE},
      {"fractal_noise", ModuleType::FRACTAL_NOISE},
      {"ridged_noise", ModuleType::RIDGED_NOISE},
      {"domain_warped_noise", ModuleType::DOMAIN_WARPED_NOISE},
      {"tree_generator", ModuleType::TREE_GENERATOR},
  };

  for (const DefaultModuleSeed& seed : defaults) {
    const std::filesystem::path path =
        moduleLibraryDirectory() /
        (std::string(seed.id) + ".fvmodule.json");
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      continue;
    }

    BiomeModule module;
    switch (seed.type) {
    case ModuleType::PERLIN_TERRAIN:
      module = BiomeModule::makePerlinTerrain(seed.id, 0);
      break;
    case ModuleType::IMPORT_VOX_FILES:
      module = BiomeModule::makeImportVoxFiles(seed.id);
      break;
    case ModuleType::GRID_PATTERN:
      module = BiomeModule::makeGridPattern(seed.id);
      break;
    case ModuleType::MENGER_SPONGE:
      module = BiomeModule::makeMengerSponge(seed.id);
      break;
    case ModuleType::CAVE_SYSTEM:
      module = BiomeModule::makeCaveSystem(seed.id);
      break;
    case ModuleType::CELLULAR_NOISE:
      module = BiomeModule::makeCellularNoise(seed.id);
      break;
    case ModuleType::FRACTAL_NOISE:
      module = BiomeModule::makeFractalNoise(seed.id);
      break;
    case ModuleType::RIDGED_NOISE:
      module = BiomeModule::makeRidgedNoise(seed.id);
      break;
    case ModuleType::DOMAIN_WARPED_NOISE:
      module = BiomeModule::makeDomainWarpedNoise(seed.id);
      break;
    case ModuleType::TREE_GENERATOR:
      module = BiomeModule::makeTreeGenerator(seed.id);
      break;
    }
    std::string error;
    if (!saveBiomeModuleToFile(path, module, error)) {
      appendLog("Failed to seed module library: " + path.string() + " | " + error);
    }
  }
}

void BiomeMakerApp::ensureDefaultPresetExists() {
  if (!presetFiles_.empty()) {
    return;
  }

  std::string error;
  BiomePreset preset = makeDefaultBiomePreset("default_perlin", 0);
  if (!preset.modules.empty()) {
    const std::filesystem::path sharedPerlin =
        defaultSharedModulePath("perlin.fvmodule.json");
    std::error_code ec;
    if (std::filesystem::exists(resolvePresetRelativePath(sharedPerlin), ec)) {
      preset.modules.front().filePath = sharedPerlin;
    }
  }
  saveBiomePresetToFile(defaultPathForPreset(preset), preset, error);
}

void BiomeMakerApp::scanPresetFiles() {
  presetFiles_.clear();

  std::error_code ec;
  if (!std::filesystem::exists(presetRootDirectory(), ec)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(presetRootDirectory(), ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() == ".json" &&
        entry.path().filename().string().find(".fvbiome.") != std::string::npos) {
      presetFiles_.push_back(entry.path());
    }
  }

  std::sort(presetFiles_.begin(), presetFiles_.end());
}

void BiomeMakerApp::scanModuleLibrary() {
  moduleLibrary_.clear();

  std::error_code ec;
  const std::filesystem::path root = moduleLibraryDirectory();
  if (!std::filesystem::exists(root, ec)) {
    selectedLibraryModuleIndex_ = 0;
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() != ".json" ||
        entry.path().filename().string().find(".fvmodule.") == std::string::npos) {
      continue;
    }

    BiomeModule module;
    std::string error;
    if (!loadBiomeModuleFromFile(entry.path(), module, error)) {
      appendLog("Skipped module file: " + entry.path().filename().string() + " | " +
                error);
      continue;
    }

    std::error_code relativeEc;
    std::filesystem::path relativePath =
        std::filesystem::relative(entry.path(), presetDirectory(), relativeEc);
    if (relativeEc) {
      relativePath = defaultSharedModulePath(entry.path().filename().string().c_str());
    }

    module.filePath = relativePath;
    moduleLibrary_.push_back({entry.path(), relativePath, module});
  }

  std::sort(moduleLibrary_.begin(), moduleLibrary_.end(),
            [](const ModuleLibraryEntry& a, const ModuleLibraryEntry& b) {
              const std::string aName = displayModuleName(a.module);
              const std::string bName = displayModuleName(b.module);
              if (aName != bName) {
                return aName < bName;
              }
              return a.relativePath.generic_string() < b.relativePath.generic_string();
            });

  if (moduleLibrary_.empty()) {
    selectedLibraryModuleIndex_ = 0;
    return;
  }

  selectedLibraryModuleIndex_ =
      std::clamp(selectedLibraryModuleIndex_, 0,
                 static_cast<int>(moduleLibrary_.size()) - 1);
}

void BiomeMakerApp::appendLog(const std::string& message) {
  logLines_.push_back(message);
  if (logLines_.size() > 200) {
    logLines_.erase(logLines_.begin());
  }
}

void BiomeMakerApp::syncPresetBuffersFromDocument() {
  copyStringToBuffer(presetIdBuffer_, currentPreset_.id);
  copyStringToBuffer(presetNameBuffer_, currentPreset_.displayName);
  copyStringToBuffer(saveAsIdBuffer_, currentPreset_.id);
  copyStringToBuffer(saveAsNameBuffer_, currentPreset_.displayName);
  clampSelectedModuleIndex();
  bufferedModuleIndex_ = -1;
}

void BiomeMakerApp::openSaveAsPopup() {
  copyStringToBuffer(saveAsIdBuffer_, currentPreset_.id);
  copyStringToBuffer(saveAsNameBuffer_, currentPreset_.displayName);
  saveAsPopupRequested_ = true;
}

void BiomeMakerApp::markDocumentDirty(const char* reason) {
  documentDirty_ = true;
  previewSourceDirty_ = true;
  if (reason && reason[0] != '\0') {
    appendLog(reason);
  }
}

void BiomeMakerApp::syncPreviewSource() {
  if (!previewSourceDirty_) {
    return;
  }

  if (ImGui::IsAnyItemActive()) {
    return;
  }

  previewWorld_.setMode(currentPreset_.preview.defaultMode);
  previewWorld_.setSandboxRadius(currentPreset_.preview.sandboxRadius);
  previewWorld_.setStreamRenderDistance(currentPreset_.preview.streamRenderDistance);
  previewWorld_.setGeneratorSource(
      std::make_shared<PresetModuleGeneratorSource>(currentPreset_));

  previewSourceDirty_ = false;
}

void BiomeMakerApp::handleShortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantCaptureKeyboard || io.WantTextInput || ImGui::IsAnyItemActive()) {
    return;
  }

  const bool ctrlDown =
      Input::keyDown(GLFW_KEY_LEFT_CONTROL) || Input::keyDown(GLFW_KEY_RIGHT_CONTROL);
  const bool shiftDown =
      Input::keyDown(GLFW_KEY_LEFT_SHIFT) || Input::keyDown(GLFW_KEY_RIGHT_SHIFT);

  if (!ctrlDown) {
    return;
  }

  if (Input::keyPressed(GLFW_KEY_N)) {
    newPresetDocument();
  } else if (Input::keyPressed(GLFW_KEY_O)) {
    openPresetPopupRequested_ = true;
  } else if (Input::keyPressed(GLFW_KEY_S) && shiftDown) {
    openSaveAsPopup();
  } else if (Input::keyPressed(GLFW_KEY_S)) {
    saveCurrentPresetDocument();
  } else if (Input::keyPressed(GLFW_KEY_D)) {
    duplicatePresetDocument();
  }
}

void BiomeMakerApp::newPresetDocument() {
  currentPreset_ = makeDefaultBiomePreset("new_perlin", 0);
  if (!currentPreset_.modules.empty()) {
    const std::filesystem::path sharedPerlin =
        defaultSharedModulePath("perlin.fvmodule.json");
    std::error_code ec;
    if (std::filesystem::exists(resolvePresetRelativePath(sharedPerlin), ec)) {
      currentPreset_.modules.front().filePath = sharedPerlin;
      currentPreset_.modules.front().displayName =
          displayModuleName(currentPreset_.modules.front());
    }
  }
  currentPresetPath_.clear();
  documentDirty_ = true;
  previewSourceDirty_ = true;
  selectedModuleIndex_ = currentPreset_.modules.empty() ? -1 : 0;
  syncPresetBuffersFromDocument();
  appendLog("Created a new preset document.");
}

void BiomeMakerApp::duplicatePresetDocument() {
  currentPreset_.id = sanitizePresetId(currentPreset_.id + "_copy");
  currentPreset_.displayName += " Copy";
  currentPresetPath_.clear();
  for (int index = 0; index < static_cast<int>(currentPreset_.modules.size()); index++) {
    BiomeModule& module = currentPreset_.modules[index];
    module.id = makeUniqueModuleId(module.id + "_copy", index);
    module.displayName = displayModuleName(module) + " Copy";
    module.filePath = makeUniqueModuleRelativePath(module.id);
    std::string error;
    if (!saveBiomeModuleToFile(resolvePresetRelativePath(module.filePath), module,
                               error)) {
      appendLog("Failed to copy module file: " + module.filePath.generic_string() +
                " | " + error);
    }
  }
  scanModuleLibrary();
  documentDirty_ = true;
  previewSourceDirty_ = true;
  selectedModuleIndex_ = currentPreset_.modules.empty() ? -1 : 0;
  syncPresetBuffersFromDocument();
  appendLog("Duplicated the current preset.");
}

bool BiomeMakerApp::loadPresetDocument(const std::filesystem::path& path) {
  BiomePreset loadedPreset;
  std::string error;
  if (!loadBiomePresetFromFile(path, loadedPreset, error)) {
    appendLog("Failed to load preset: " + path.string() + " | " + error);
    return false;
  }

  for (BiomeModule& module : loadedPreset.modules) {
    module.displayName = displayModuleName(module);
  }

  currentPreset_ = loadedPreset;
  currentPresetPath_ = path;
  documentDirty_ = false;
  previewSourceDirty_ = true;
  selectedModuleIndex_ = currentPreset_.modules.empty() ? -1 : 0;
  syncPresetBuffersFromDocument();
  appendLog("Loaded preset: " + path.filename().string());
  return true;
}

bool BiomeMakerApp::saveCurrentPresetDocument() {
  if (currentPresetPath_.empty()) {
    openSaveAsPopup();
    return false;
  }

  return saveCurrentPresetDocumentAs(currentPresetPath_);
}

bool BiomeMakerApp::saveCurrentPresetDocumentAs(const std::filesystem::path& path) {
  currentPreset_.id = sanitizePresetId(currentPreset_.id);
  for (int index = 0; index < static_cast<int>(currentPreset_.modules.size()); index++) {
    BiomeModule& module = currentPreset_.modules[static_cast<std::size_t>(index)];
    module.id = makeUniqueModuleId(module.id, index);
    if (module.filePath.empty()) {
      module.filePath = makeUniqueModuleRelativePath(module.id);
    }
  }
  syncPresetBuffersFromDocument();

  std::string error;
  if (!saveBiomePresetToFile(path, currentPreset_, error)) {
    appendLog("Failed to save preset: " + path.string() + " | " + error);
    return false;
  }

  for (BiomeModule& module : currentPreset_.modules) {
    if (module.filePath.empty()) {
      module.filePath = suggestModulePath(currentPreset_, module);
    }
  }
  currentPresetPath_ = path;
  documentDirty_ = false;
  scanPresetFiles();
  scanModuleLibrary();
  appendLog("Saved preset: " + path.filename().string());
  return true;
}

bool BiomeMakerApp::revertCurrentPresetDocument() {
  if (currentPresetPath_.empty()) {
    return false;
  }

  if (!loadPresetDocument(currentPresetPath_)) {
    return false;
  }

  appendLog("Reverted preset to the version on disk.");
  return true;
}

std::filesystem::path BiomeMakerApp::defaultPathForPreset(
    const BiomePreset& preset) const {
  return presetRootDirectory() /
         (sanitizePresetId(preset.id) + ".fvbiome.json");
}

std::filesystem::path BiomeMakerApp::presetDirectory() const {
  const std::filesystem::path presetPath =
      currentPresetPath_.empty() ? defaultPathForPreset(currentPreset_)
                                 : currentPresetPath_;
  return presetPath.parent_path();
}

std::filesystem::path BiomeMakerApp::resolvePresetRelativePath(
    const std::filesystem::path& relativePath) const {
  if (relativePath.is_absolute()) {
    return relativePath;
  }
  return presetDirectory() / relativePath;
}

std::filesystem::path BiomeMakerApp::makeUniqueModuleRelativePath(
    const std::string& baseName,
    const std::filesystem::path& ignoreRelativePath) const {
  const std::string sanitizedBase = sanitizeModuleId(baseName);
  const std::filesystem::path ignoredNormalized = ignoreRelativePath.lexically_normal();
  const std::filesystem::path privateModuleDirectory =
      defaultPrivateModuleDirectory(currentPreset_.id);

  for (int suffix = 1;; suffix++) {
    const std::string fileName =
        suffix == 1 ? sanitizedBase + ".fvmodule.json"
                    : sanitizedBase + "_" + std::to_string(suffix) + ".fvmodule.json";
    const std::filesystem::path candidate =
        (privateModuleDirectory / fileName).lexically_normal();
    if (!ignoredNormalized.empty() && candidate == ignoredNormalized) {
      return candidate;
    }

    std::error_code ec;
    if (!std::filesystem::exists(resolvePresetRelativePath(candidate), ec)) {
      return candidate;
    }
  }
}

bool BiomeMakerApp::isSharedLibraryModulePath(
    const std::filesystem::path& path) const {
  if (path.empty()) {
    return false;
  }

  if (path.is_absolute()) {
    std::error_code ec;
    const std::filesystem::path canonicalLibraryDir =
        std::filesystem::weakly_canonical(moduleLibraryDirectory(), ec);
    if (ec) {
      return false;
    }

    ec.clear();
    const std::filesystem::path canonicalPath =
        std::filesystem::weakly_canonical(path, ec);
    if (ec) {
      return false;
    }
    return canonicalPath.parent_path() == canonicalLibraryDir;
  }

  const std::string generic = path.lexically_normal().generic_string();
  return generic == "../Modules" || generic.rfind("../Modules/", 0) == 0;
}

void BiomeMakerApp::clampSelectedModuleIndex() {
  if (currentPreset_.modules.empty()) {
    selectedModuleIndex_ = -1;
    return;
  }
  selectedModuleIndex_ =
      std::clamp(selectedModuleIndex_, 0,
                 static_cast<int>(currentPreset_.modules.size()) - 1);
}

BiomeModule* BiomeMakerApp::selectedModule() {
  clampSelectedModuleIndex();
  if (selectedModuleIndex_ < 0) {
    return nullptr;
  }
  return &currentPreset_.modules[static_cast<std::size_t>(selectedModuleIndex_)];
}

const BiomeModule* BiomeMakerApp::selectedModule() const {
  if (currentPreset_.modules.empty()) {
    return nullptr;
  }
  const int clampedIndex =
      std::clamp(selectedModuleIndex_, 0,
                 static_cast<int>(currentPreset_.modules.size()) - 1);
  return &currentPreset_.modules[static_cast<std::size_t>(clampedIndex)];
}

void BiomeMakerApp::syncSelectedModuleBuffers() {
  const BiomeModule* module = selectedModule();
  if (!module) {
    moduleIdBuffer_[0] = '\0';
    moduleNameBuffer_[0] = '\0';
    moduleSourceDirBuffer_[0] = '\0';
    bufferedModuleIndex_ = -1;
    return;
  }

  if (bufferedModuleIndex_ == selectedModuleIndex_) {
    return;
  }

  copyStringToBuffer(moduleIdBuffer_, module->id);
  copyStringToBuffer(moduleNameBuffer_, module->displayName);
  if (module->type == ModuleType::IMPORT_VOX_FILES) {
    copyStringToBuffer(moduleSourceDirBuffer_, module->importVoxFiles.sourceDirectory);
  } else {
    moduleSourceDirBuffer_[0] = '\0';
  }
  bufferedModuleIndex_ = selectedModuleIndex_;
}

void BiomeMakerApp::markSelectedModuleDirty(const char* reason) {
  if (BiomeModule* module = selectedModule()) {
    if (isSharedLibraryModulePath(module->filePath)) {
      module->filePath = makeUniqueModuleRelativePath(module->id);
    }
  }

  markDocumentDirty(reason);
}

std::string BiomeMakerApp::makeUniqueModuleId(const std::string& base,
                                              int ignoreIndex) const {
  const std::string sanitizedBase = sanitizeModuleId(base);
  std::string candidate = sanitizedBase;
  int suffix = 2;

  const auto isTaken = [&](const std::string& value) {
    for (int index = 0; index < static_cast<int>(currentPreset_.modules.size()); index++) {
      if (index == ignoreIndex) {
        continue;
      }
      if (currentPreset_.modules[static_cast<std::size_t>(index)].id == value) {
        return true;
      }
    }
    return false;
  };

  while (isTaken(candidate)) {
    candidate = sanitizedBase + "_" + std::to_string(suffix++);
  }
  return candidate;
}

void BiomeMakerApp::addModuleFromLibrary(int libraryIndex) {
  if (libraryIndex < 0 || libraryIndex >= static_cast<int>(moduleLibrary_.size())) {
    return;
  }

  BiomeModule module = moduleLibrary_[static_cast<std::size_t>(libraryIndex)].module;
  module.id = makeUniqueModuleId(module.id);
  module.displayName = displayModuleName(module);
  module.filePath = moduleLibrary_[static_cast<std::size_t>(libraryIndex)].relativePath;
  currentPreset_.modules.push_back(module);
  selectedModuleIndex_ = static_cast<int>(currentPreset_.modules.size()) - 1;
  bufferedModuleIndex_ = -1;
  markDocumentDirty("Added a module from the folder.");
}

void BiomeMakerApp::duplicateSelectedModule() {
  BiomeModule* module = selectedModule();
  if (!module) {
    return;
  }

  BiomeModule copy = *module;
  copy.id = makeUniqueModuleId(copy.id + "_copy");
  copy.displayName = displayModuleName(copy) + " Copy";
  copy.filePath = makeUniqueModuleRelativePath(copy.id);
  std::string error;
  if (!saveBiomeModuleToFile(resolvePresetRelativePath(copy.filePath), copy, error)) {
    appendLog("Failed to duplicate module file: " + copy.filePath.generic_string() +
              " | " + error);
    return;
  }
  scanModuleLibrary();
  currentPreset_.modules.insert(
      currentPreset_.modules.begin() + selectedModuleIndex_ + 1, copy);
  selectedModuleIndex_++;
  bufferedModuleIndex_ = -1;
  markDocumentDirty("Duplicated a module layer.");
}

void BiomeMakerApp::removeSelectedModule() {
  if (selectedModuleIndex_ < 0 ||
      selectedModuleIndex_ >= static_cast<int>(currentPreset_.modules.size())) {
    return;
  }

  currentPreset_.modules.erase(
      currentPreset_.modules.begin() + selectedModuleIndex_);
  if (selectedModuleIndex_ >= static_cast<int>(currentPreset_.modules.size())) {
    selectedModuleIndex_ = static_cast<int>(currentPreset_.modules.size()) - 1;
  }
  bufferedModuleIndex_ = -1;
  markDocumentDirty("Removed a module layer.");
}

void BiomeMakerApp::moveSelectedModule(int direction) {
  const int otherIndex = selectedModuleIndex_ + direction;
  if (selectedModuleIndex_ < 0 ||
      otherIndex < 0 ||
      otherIndex >= static_cast<int>(currentPreset_.modules.size())) {
    return;
  }

  std::swap(currentPreset_.modules[static_cast<std::size_t>(selectedModuleIndex_)],
            currentPreset_.modules[static_cast<std::size_t>(otherIndex)]);
  selectedModuleIndex_ = otherIndex;
  bufferedModuleIndex_ = -1;
  markDocumentDirty("Reordered module layers.");
}

int BiomeMakerApp::countImportVoxelFiles(const ImportVoxFilesModule& module) const {
  std::error_code ec;
  const std::filesystem::path root = AppPaths::resolve(module.sourceDirectory);
  if (!std::filesystem::exists(root, ec)) {
    return 0;
  }

  int count = 0;
  if (module.includeSubdirectories) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
      if (ec) {
        break;
      }
      if (entry.is_regular_file() && entry.path().extension() == ".vox") {
        count++;
      }
    }
    return count;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_regular_file() && entry.path().extension() == ".vox") {
      count++;
    }
  }
  return count;
}

void BiomeMakerApp::drawUi(float dtSeconds, float timeSeconds) {
  drawDockspace();
  drawMenuBar();
  drawSourceBrowser();
  drawInspector();
  syncPreviewSource();
  drawViewportWindow(dtSeconds, timeSeconds);
  drawStatsWindow();
  drawLogWindow();
  drawOpenPresetPopup();
  drawSaveAsPopup();
}

void BiomeMakerApp::drawDockspace() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (!viewport) {
    return;
  }

  const ImGuiID dockspaceId =
      ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_None);

  if (dockLayoutInitialized_) {
    return;
  }

  dockLayoutInitialized_ = true;
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

  ImGuiID leftNode = 0;
  ImGuiID rightNode = 0;
  ImGuiID bottomNode = 0;
  ImGuiID centerNode = dockspaceId;

  ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Left, 0.18f, &leftNode, &centerNode);
  ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Right, 0.23f, &rightNode,
                              &centerNode);
  ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Down, 0.24f, &bottomNode,
                              &centerNode);

  ImGuiID statsNode = 0;
  ImGuiID sourcesNode = leftNode;
  ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Down, 0.34f, &statsNode, &sourcesNode);

  ImGui::DockBuilderDockWindow("Sources", sourcesNode);
  ImGui::DockBuilderDockWindow("Stats", statsNode);
  ImGui::DockBuilderDockWindow("Inspector", rightNode);
  ImGui::DockBuilderDockWindow("Viewport", centerNode);
  ImGui::DockBuilderDockWindow("Log", bottomNode);
  ImGui::DockBuilderFinish(dockspaceId);

  ImGui::SaveIniSettingsToDisk(editorIniPath().string().c_str());
}

void BiomeMakerApp::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New", "Ctrl+N")) {
      newPresetDocument();
    }
    if (ImGui::MenuItem("Open...", "Ctrl+O")) {
      openPresetPopupRequested_ = true;
    }

    constexpr bool canSave = true;
    if (ImGui::MenuItem("Save", "Ctrl+S", false, canSave)) {
      saveCurrentPresetDocument();
    }
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, canSave)) {
      openSaveAsPopup();
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, canSave)) {
      duplicatePresetDocument();
    }
    if (ImGui::MenuItem("Revert to Disk", nullptr, false,
                        canSave && !currentPresetPath_.empty())) {
      revertCurrentPresetDocument();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) {
      glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    ImGui::EndMenu();
  }

  ImGui::TextUnformatted("Editable Preset");
  ImGui::SameLine();
  std::string title = currentPreset_.displayName;
  if (documentDirty_) {
    title += " *";
  }
  ImGui::TextUnformatted(title.c_str());

  ImGui::EndMainMenuBar();
}

void BiomeMakerApp::drawSourceBrowser() {
  if (!ImGui::Begin("Sources")) {
    ImGui::End();
    return;
  }

  if (ImGui::CollapsingHeader("Preset Files", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const std::filesystem::path& path : presetFiles_) {
      const bool selected = currentPresetPath_ == path;
      if (ImGui::Selectable(path.filename().string().c_str(), selected)) {
        loadPresetDocument(path);
      }
    }
    if (presetFiles_.empty()) {
      ImGui::TextDisabled("No preset files found.");
    }
  }

  if (ImGui::CollapsingHeader("Module Files", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (moduleLibrary_.empty()) {
      ImGui::TextDisabled("No module files found in Resources/World/Modules.");
    } else {
      for (const ModuleLibraryEntry& entry : moduleLibrary_) {
        ImGui::Text("%s", displayModuleName(entry.module).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", displayModuleFileName(entry.relativePath).c_str());
      }
    }
  }

  ImGui::Separator();
  if (ImGui::Button("New Preset")) {
    newPresetDocument();
  }
  ImGui::SameLine();
  if (ImGui::Button("Open...")) {
    openPresetPopupRequested_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh Modules")) {
    scanModuleLibrary();
  }

  ImGui::End();
}

void BiomeMakerApp::drawInspector() {
  if (!ImGui::Begin("Inspector")) {
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Viewport");
  if (ImGui::Checkbox("Enable Fog", &viewportFogEnabled_)) {
    appendLog(viewportFogEnabled_ ? "Enabled viewport fog."
                                  : "Disabled viewport fog.");
  }
  ImGui::Separator();

  if (ImGui::InputText("Preset Id", presetIdBuffer_, sizeof(presetIdBuffer_))) {
    currentPreset_.id = sanitizePresetId(presetIdBuffer_);
    copyStringToBuffer(presetIdBuffer_, currentPreset_.id);
    markDocumentDirty("Updated preset id.");
  }
  if (ImGui::InputText("Display Name", presetNameBuffer_, sizeof(presetNameBuffer_))) {
    currentPreset_.displayName = presetNameBuffer_;
    markDocumentDirty("Updated preset display name.");
  }

  int seed = static_cast<int>(currentPreset_.preview.seed);
  if (ImGui::InputInt("Seed", &seed)) {
    currentPreset_.preview.seed = static_cast<std::uint32_t>(std::max(seed, 0));
    markDocumentDirty("Updated preset seed.");
  }

  int depth = currentPreset_.preview.depth;
  if (ImGui::SliderInt("Depth", &depth, 0, 12)) {
    currentPreset_.preview.depth = depth;
    markDocumentDirty("Updated preset depth.");
  }

  const int currentModeIndex =
      currentPreset_.preview.defaultMode == PreviewMode::SANDBOX ? 0 : 1;
  int modeIndex = currentModeIndex;
  const char* modes[] = {"Sandbox", "Streaming"};
  if (ImGui::Combo("Default Mode", &modeIndex, modes, 2)) {
    currentPreset_.preview.defaultMode =
        modeIndex == 0 ? PreviewMode::SANDBOX : PreviewMode::STREAMING;
    markDocumentDirty("Updated preset preview mode.");
  }

  std::array<int, 3> sandboxRadius = {currentPreset_.preview.sandboxRadius.x,
                                      currentPreset_.preview.sandboxRadius.y,
                                      currentPreset_.preview.sandboxRadius.z};
  if (ImGui::InputInt3("Sandbox Radius", sandboxRadius.data())) {
    currentPreset_.preview.sandboxRadius =
        glm::max(glm::ivec3(sandboxRadius[0], sandboxRadius[1], sandboxRadius[2]),
                 glm::ivec3(0));
    markDocumentDirty("Updated sandbox radius.");
  }

  int streamRenderDistance = currentPreset_.preview.streamRenderDistance;
  if (ImGui::SliderInt("Stream Distance", &streamRenderDistance, 2, 10)) {
    currentPreset_.preview.streamRenderDistance = streamRenderDistance;
    markDocumentDirty("Updated stream render distance.");
  }
  ImGui::SeparatorText("Layers");
  ImGui::TextDisabled("Top layer = highest priority");
  clampSelectedModuleIndex();
  syncSelectedModuleBuffers();

    for (int index = 0; index < static_cast<int>(currentPreset_.modules.size()); index++) {
      const BiomeModule& module =
          currentPreset_.modules[static_cast<std::size_t>(index)];
      const bool selected = index == selectedModuleIndex_;
      const std::string label =
          std::to_string(index + 1) + ". " + displayModuleName(module) + "##" +
          module.id;
      if (ImGui::Selectable(label.c_str(), selected)) {
        selectedModuleIndex_ = index;
        bufferedModuleIndex_ = -1;
        syncSelectedModuleBuffers();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s%s", displayModuleFileName(module.filePath).c_str(),
                          module.enabled ? "" : " disabled");
    }

    if (currentPreset_.modules.empty()) {
      ImGui::TextDisabled("No module layers in this preset.");
    }

    ImGui::SeparatorText("Module Library");
    ImGui::TextDisabled("Only files from Resources/World/Modules can be added to the scene.");
    if (moduleLibrary_.empty()) {
      ImGui::TextDisabled("No .fvmodule.json files found.");
    } else {
      selectedLibraryModuleIndex_ =
          std::clamp(selectedLibraryModuleIndex_, 0,
                     static_cast<int>(moduleLibrary_.size()) - 1);
      const ModuleLibraryEntry& selectedLibraryModule =
          moduleLibrary_[static_cast<std::size_t>(selectedLibraryModuleIndex_)];
      if (ImGui::BeginCombo("Available Modules",
                            displayModuleName(selectedLibraryModule.module).c_str())) {
        for (int index = 0; index < static_cast<int>(moduleLibrary_.size()); index++) {
          const ModuleLibraryEntry& entry =
              moduleLibrary_[static_cast<std::size_t>(index)];
          const bool selected = index == selectedLibraryModuleIndex_;
          const std::string label = displayModuleName(entry.module) + "##library_" +
                                    std::to_string(index);
          if (ImGui::Selectable(label.c_str(), selected)) {
            selectedLibraryModuleIndex_ = index;
          }
          ImGui::SameLine();
          ImGui::TextDisabled("%s", displayModuleFileName(entry.relativePath).c_str());
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      ImGui::TextDisabled("Selected File: %s",
                          selectedLibraryModule.relativePath.generic_string().c_str());
      if (ImGui::Button("Add Module")) {
        addModuleFromLibrary(selectedLibraryModuleIndex_);
      }
      ImGui::SameLine();
      if (ImGui::Button("Refresh Library")) {
        scanModuleLibrary();
      }
    }

    const bool hasSelectedModule = selectedModule() != nullptr;
    if (ImGui::Button("Duplicate Layer") && hasSelectedModule) {
      duplicateSelectedModule();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Layer") && hasSelectedModule) {
      removeSelectedModule();
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Up") && hasSelectedModule) {
      moveSelectedModule(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Down") && hasSelectedModule) {
      moveSelectedModule(1);
    }

    if (BiomeModule* module = selectedModule()) {
      syncSelectedModuleBuffers();
      ImGui::SeparatorText("Selected Layer");

      if (ImGui::InputText("Layer Id", moduleIdBuffer_, sizeof(moduleIdBuffer_))) {
        module->id = makeUniqueModuleId(moduleIdBuffer_, selectedModuleIndex_);
        copyStringToBuffer(moduleIdBuffer_, module->id);
        module->filePath = makeUniqueModuleRelativePath(module->id, module->filePath);
        markSelectedModuleDirty("Updated layer id.");
      }
      if (ImGui::InputText("Layer Name", moduleNameBuffer_, sizeof(moduleNameBuffer_))) {
        module->displayName = moduleNameBuffer_;
        markSelectedModuleDirty("Updated layer display name.");
      }
      if (ImGui::Checkbox("Enabled", &module->enabled)) {
        markSelectedModuleDirty("Updated layer enabled state.");
      }

      int blendModeIndex = static_cast<int>(module->blendMode);
      const char* blendModes[] = {"Overwrite All", "Place Solids", "Place On Air"};
      if (ImGui::Combo("Blend Mode", &blendModeIndex, blendModes, 3)) {
        module->blendMode = static_cast<LayerBlendMode>(blendModeIndex);
        markSelectedModuleDirty("Updated layer blend mode.");
      }

      const std::filesystem::path modulePath =
          module->filePath.empty() ? makeUniqueModuleRelativePath(module->id)
                                   : module->filePath;
      const std::filesystem::path resolvedModulePath =
          resolvePresetRelativePath(modulePath);
      if (module->filePath.empty()) {
        ImGui::TextDisabled("Module File (created on save): %s",
                            modulePath.generic_string().c_str());
      } else if (std::filesystem::exists(resolvedModulePath)) {
        ImGui::TextDisabled("Module File: %s", modulePath.generic_string().c_str());
      } else {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                           "Module File Missing: %s",
                           modulePath.generic_string().c_str());
      }

      const auto drawTricolorPalette = [&](TricolorPaletteModule& palette) {
        ImGui::SeparatorText("Voxel Palette");
        if (drawBlockTypeCombo("Primary", palette.primary)) {
          markSelectedModuleDirty("Updated primary palette block.");
        }
        if (drawBlockTypeCombo("Secondary", palette.secondary)) {
          markSelectedModuleDirty("Updated secondary palette block.");
        }
        if (drawBlockTypeCombo("Accent", palette.accent)) {
          markSelectedModuleDirty("Updated accent palette block.");
        }
      };

      const auto drawVolumeNoiseSettings = [&](VolumeNoiseModuleSettings& noise) {
        std::array<int, 3> offset = {noise.offset.x, noise.offset.y, noise.offset.z};
        if (ImGui::InputInt3("Offset", offset.data())) {
          noise.offset = glm::ivec3(offset[0], offset[1], offset[2]);
          markSelectedModuleDirty("Updated noise offset.");
        }

        if (ImGui::SliderFloat("Base Scale", &noise.baseScale, 0.001f, 0.25f,
                               "%.4f", ImGuiSliderFlags_Logarithmic)) {
          noise.baseScale = std::max(noise.baseScale, 0.0001f);
          markSelectedModuleDirty("Updated noise base scale.");
        }
        if (ImGui::SliderInt("Octaves", &noise.octaves, 1, 8)) {
          markSelectedModuleDirty("Updated noise octaves.");
        }
        if (ImGui::SliderFloat("Persistence", &noise.persistence, 0.05f, 0.95f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated noise persistence.");
        }
        if (ImGui::SliderFloat("Lacunarity", &noise.lacunarity, 1.05f, 6.0f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated noise lacunarity.");
        }
        if (ImGui::SliderFloat("Threshold", &noise.threshold, -1.0f, 1.0f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated noise threshold.");
        }
        if (ImGui::Checkbox("Invert Pattern", &noise.invert)) {
          markSelectedModuleDirty(noise.invert ? "Enabled noise inversion."
                                               : "Disabled noise inversion.");
        }

        if (ImGui::Checkbox("Infinite Y", &noise.infiniteY)) {
          markSelectedModuleDirty(noise.infiniteY ? "Enabled infinite Y range."
                                                  : "Disabled infinite Y range.");
        }
        int minY = noise.minY;
        int maxY = noise.maxY;
        ImGui::BeginDisabled(noise.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          noise.minY = minY;
          if (noise.maxY < noise.minY) {
            noise.maxY = noise.minY;
          }
          markSelectedModuleDirty("Updated noise minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          noise.maxY = std::max(maxY, noise.minY);
          markSelectedModuleDirty("Updated noise maximum height.");
        }
        ImGui::EndDisabled();

        if (ImGui::SliderFloat("Secondary Noise Scale", &noise.secondaryNoiseScale,
                               0.0f, 0.25f, "%.3f")) {
          markSelectedModuleDirty("Updated secondary noise scale.");
        }
        if (ImGui::SliderFloat("Secondary Threshold", &noise.secondaryThreshold,
                               -1.0f, 1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated secondary threshold.");
        }
        if (ImGui::SliderFloat("Accent Noise Scale", &noise.accentNoiseScale, 0.0f,
                               0.25f, "%.3f")) {
          markSelectedModuleDirty("Updated accent noise scale.");
        }
        if (ImGui::SliderFloat("Accent Threshold", &noise.accentThreshold, -1.0f,
                               1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated accent threshold.");
        }

        drawTricolorPalette(noise.palette);
      };

      if (module->type == ModuleType::PERLIN_TERRAIN) {
        ImGui::SeparatorText("Perlin");
        PerlinDensityModule& density = module->perlinTerrain.density;
        MaterialPaletteModule& palette = module->perlinTerrain.palette;

        if (ImGui::SliderFloat("Base Scale", &density.baseScale, 0.005f, 0.20f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated base scale.");
        }
        if (ImGui::SliderFloat("Density Threshold", &density.densityThreshold,
                               -0.20f, 0.80f, "%.3f")) {
          markSelectedModuleDirty("Updated density threshold.");
        }
        if (ImGui::SliderInt("Density Octaves", &density.densityOctaves, 1, 8)) {
          markSelectedModuleDirty("Updated density octaves.");
        }
        if (ImGui::SliderFloat("Density Persistence", &density.densityPersistence,
                               0.10f, 0.95f, "%.3f")) {
          markSelectedModuleDirty("Updated density persistence.");
        }
        if (ImGui::SliderInt("Warp Octaves", &density.warpOctaves, 1, 6)) {
          markSelectedModuleDirty("Updated warp octaves.");
        }
        if (ImGui::SliderFloat("Warp Strength", &density.warpStrength, 0.0f, 2.0f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated warp strength.");
        }

        ImGui::SeparatorText("Material Palette");
        if (drawBlockTypeCombo("Surface Rib", palette.surfaceRib)) {
          markSelectedModuleDirty("Updated surface rib block.");
        }
        if (drawBlockTypeCombo("Surface Patch", palette.surfacePatch)) {
          markSelectedModuleDirty("Updated surface patch block.");
        }
        if (drawBlockTypeCombo("Shell", palette.shell)) {
          markSelectedModuleDirty("Updated shell block.");
        }
        if (drawBlockTypeCombo("Core", palette.core)) {
          markSelectedModuleDirty("Updated core block.");
        }
        if (drawBlockTypeCombo("Accent", palette.accent)) {
          markSelectedModuleDirty("Updated accent block.");
        }
        if (drawBlockTypeCombo("Recess", palette.recess)) {
          markSelectedModuleDirty("Updated recess block.");
        }
      } else if (module->type == ModuleType::IMPORT_VOX_FILES) {
        ImGui::SeparatorText("Vox Import");
        ImportVoxFilesModule& vox = module->importVoxFiles;

        if (ImGui::InputText("Source Directory", moduleSourceDirBuffer_,
                             sizeof(moduleSourceDirBuffer_))) {
          vox.sourceDirectory = moduleSourceDirBuffer_;
          markSelectedModuleDirty("Updated VOX source directory.");
        }
        if (ImGui::Checkbox("Include Subdirectories", &vox.includeSubdirectories)) {
          markSelectedModuleDirty("Updated recursive VOX import.");
        }

        int patternIndex = static_cast<int>(vox.pattern);
        const char* patterns[] = {"Random Scatter", "Grid"};
        if (ImGui::Combo("Pattern", &patternIndex, patterns, 2)) {
          vox.pattern = static_cast<VoxPlacementPattern>(patternIndex);
          markSelectedModuleDirty("Updated VOX placement pattern.");
        }

        std::array<int, 2> cellSize = {vox.cellSize.x, vox.cellSize.y};
        if (ImGui::InputInt2("Cell Size X/Z", cellSize.data())) {
          vox.cellSize = glm::max(glm::ivec2(cellSize[0], cellSize[1]), glm::ivec2(1));
          markSelectedModuleDirty("Updated VOX cell size.");
        }

        std::array<int, 2> jitter = {vox.jitter.x, vox.jitter.y};
        if (ImGui::InputInt2("Jitter X/Z", jitter.data())) {
          vox.jitter = glm::max(glm::ivec2(jitter[0], jitter[1]), glm::ivec2(0));
          markSelectedModuleDirty("Updated VOX jitter.");
        }

        if (ImGui::Checkbox("Infinite Y", &vox.infiniteY)) {
          markSelectedModuleDirty(vox.infiniteY ? "Enabled infinite VOX Y placement."
                                                : "Disabled infinite VOX Y placement.");
        }

        int minY = vox.minY;
        int maxY = vox.maxY;
        ImGui::BeginDisabled(vox.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          vox.minY = minY;
          if (vox.maxY < vox.minY) {
            vox.maxY = vox.minY;
          }
          markSelectedModuleDirty("Updated VOX minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          vox.maxY = std::max(maxY, vox.minY);
          markSelectedModuleDirty("Updated VOX maximum height.");
        }
        ImGui::EndDisabled();

        if (ImGui::SliderFloat("Spawn Chance", &vox.spawnChance, 0.0f, 1.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated VOX spawn chance.");
        }

        int rotationModeIndex = static_cast<int>(vox.rotationMode);
        const char* rotationModes[] = {"Fixed", "Random 90"};
        if (ImGui::Combo("Rotation Mode", &rotationModeIndex, rotationModes, 2)) {
          vox.rotationMode = static_cast<VoxRotationMode>(rotationModeIndex);
          markSelectedModuleDirty("Updated VOX rotation mode.");
        }

        ImGui::BeginDisabled(vox.colorMapping != VoxColorMapping::DEFAULT);
        if (drawBlockTypeCombo("Default Voxel", vox.defaultVoxel)) {
          markSelectedModuleDirty("Updated VOX default voxel.");
        }
        ImGui::EndDisabled();

        if (vox.rotationMode == VoxRotationMode::FIXED) {
          int fixedRotation = vox.fixedRotation;
          const char* fixedRotations[] = {"0", "90", "180", "270"};
          if (ImGui::Combo("Fixed Rotation", &fixedRotation, fixedRotations, 4)) {
            vox.fixedRotation = fixedRotation;
            markSelectedModuleDirty("Updated VOX fixed rotation.");
          }
        }

        int colorMappingIndex = static_cast<int>(vox.colorMapping);
        const char* colorMappings[] = {"Default", "Ancient Ruins"};
        if (ImGui::Combo("Color Mapping", &colorMappingIndex, colorMappings, 2)) {
          vox.colorMapping = static_cast<VoxColorMapping>(colorMappingIndex);
          markSelectedModuleDirty("Updated VOX color mapping.");
        }

        int seedOffset = static_cast<int>(vox.seedOffset);
        if (ImGui::InputInt("Seed Offset", &seedOffset)) {
          vox.seedOffset = static_cast<std::uint32_t>(std::max(seedOffset, 0));
          markSelectedModuleDirty("Updated VOX seed offset.");
        }

        ImGui::Text("Matching .vox Files: %d", countImportVoxelFiles(vox));
      } else if (module->type == ModuleType::GRID_PATTERN) {
        ImGui::SeparatorText("Grid Pattern");
        GridPatternModule& grid = module->gridPattern;

        std::array<int, 3> cellSize = {grid.cellSize.x, grid.cellSize.y, grid.cellSize.z};
        if (ImGui::InputInt3("Cell Size", cellSize.data())) {
          grid.cellSize = glm::max(glm::ivec3(cellSize[0], cellSize[1], cellSize[2]),
                                   glm::ivec3(1));
          grid.lineWidth = glm::min(grid.lineWidth, grid.cellSize);
          markSelectedModuleDirty("Updated grid cell size.");
        }

        std::array<int, 3> lineWidth = {grid.lineWidth.x, grid.lineWidth.y,
                                        grid.lineWidth.z};
        if (ImGui::InputInt3("Line Width", lineWidth.data())) {
          grid.lineWidth =
              glm::min(glm::max(glm::ivec3(lineWidth[0], lineWidth[1], lineWidth[2]),
                                glm::ivec3(1)),
                       grid.cellSize);
          markSelectedModuleDirty("Updated grid line width.");
        }

        std::array<int, 3> offset = {grid.offset.x, grid.offset.y, grid.offset.z};
        if (ImGui::InputInt3("Offset", offset.data())) {
          grid.offset = glm::ivec3(offset[0], offset[1], offset[2]);
          markSelectedModuleDirty("Updated grid offset.");
        }

        if (ImGui::SliderInt("Min Axis Matches", &grid.minAxisMatches, 1, 3)) {
          grid.maxAxisMatches = std::max(grid.maxAxisMatches, grid.minAxisMatches);
          markSelectedModuleDirty("Updated grid minimum axis matches.");
        }
        if (ImGui::SliderInt("Max Axis Matches", &grid.maxAxisMatches,
                             grid.minAxisMatches, 3)) {
          markSelectedModuleDirty("Updated grid maximum axis matches.");
        }

        if (ImGui::Checkbox("Infinite Y", &grid.infiniteY)) {
          markSelectedModuleDirty(grid.infiniteY ? "Enabled infinite grid Y."
                                                 : "Disabled infinite grid Y.");
        }
        int minY = grid.minY;
        int maxY = grid.maxY;
        ImGui::BeginDisabled(grid.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          grid.minY = minY;
          if (grid.maxY < grid.minY) {
            grid.maxY = grid.minY;
          }
          markSelectedModuleDirty("Updated grid minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          grid.maxY = std::max(maxY, grid.minY);
          markSelectedModuleDirty("Updated grid maximum height.");
        }
        ImGui::EndDisabled();

        if (ImGui::SliderFloat("Warp Scale", &grid.warpScale, 0.0f, 0.20f, "%.3f")) {
          markSelectedModuleDirty("Updated grid warp scale.");
        }
        if (ImGui::SliderFloat("Warp Strength", &grid.warpStrength, 0.0f, 16.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated grid warp strength.");
        }
        if (ImGui::SliderFloat("Accent Noise Scale", &grid.accentNoiseScale, 0.0f,
                               0.25f, "%.3f")) {
          markSelectedModuleDirty("Updated grid accent noise scale.");
        }
        if (ImGui::SliderFloat("Accent Threshold", &grid.accentThreshold, -1.0f,
                               1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated grid accent threshold.");
        }

        drawTricolorPalette(grid.palette);
      } else if (module->type == ModuleType::MENGER_SPONGE) {
        ImGui::SeparatorText("Menger Sponge");
        MengerSpongeModule& sponge = module->mengerSponge;

        std::array<int, 3> cellSize = {sponge.cellSize.x, sponge.cellSize.y,
                                       sponge.cellSize.z};
        if (ImGui::InputInt3("Cell Size", cellSize.data())) {
          sponge.cellSize =
              glm::max(glm::ivec3(cellSize[0], cellSize[1], cellSize[2]),
                       glm::ivec3(3));
          markSelectedModuleDirty("Updated sponge cell size.");
        }

        std::array<int, 3> offset = {sponge.offset.x, sponge.offset.y, sponge.offset.z};
        if (ImGui::InputInt3("Offset", offset.data())) {
          sponge.offset = glm::ivec3(offset[0], offset[1], offset[2]);
          markSelectedModuleDirty("Updated sponge offset.");
        }

        if (ImGui::SliderInt("Iterations", &sponge.iterations, 1, 7)) {
          markSelectedModuleDirty("Updated sponge iterations.");
        }
        if (ImGui::SliderInt("Void Axis Threshold", &sponge.voidAxisThreshold, 1, 3)) {
          markSelectedModuleDirty("Updated sponge void axis threshold.");
        }
        if (ImGui::Checkbox("Invert Pattern", &sponge.invert)) {
          markSelectedModuleDirty(sponge.invert ? "Enabled sponge inversion."
                                                : "Disabled sponge inversion.");
        }

        if (ImGui::Checkbox("Infinite Y", &sponge.infiniteY)) {
          markSelectedModuleDirty(sponge.infiniteY ? "Enabled infinite sponge Y."
                                                   : "Disabled infinite sponge Y.");
        }
        int minY = sponge.minY;
        int maxY = sponge.maxY;
        ImGui::BeginDisabled(sponge.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          sponge.minY = minY;
          if (sponge.maxY < sponge.minY) {
            sponge.maxY = sponge.minY;
          }
          markSelectedModuleDirty("Updated sponge minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          sponge.maxY = std::max(maxY, sponge.minY);
          markSelectedModuleDirty("Updated sponge maximum height.");
        }
        ImGui::EndDisabled();

        if (ImGui::SliderFloat("Warp Scale", &sponge.warpScale, 0.0f, 0.20f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated sponge warp scale.");
        }
        if (ImGui::SliderFloat("Warp Strength", &sponge.warpStrength, 0.0f, 12.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated sponge warp strength.");
        }
        if (ImGui::SliderFloat("Accent Noise Scale", &sponge.accentNoiseScale, 0.0f,
                               0.25f, "%.3f")) {
          markSelectedModuleDirty("Updated sponge accent noise scale.");
        }
        if (ImGui::SliderFloat("Accent Threshold", &sponge.accentThreshold, -1.0f,
                               1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated sponge accent threshold.");
        }

        drawTricolorPalette(sponge.palette);
      } else if (module->type == ModuleType::CAVE_SYSTEM) {
        ImGui::SeparatorText("Cave System");
        CaveSystemModule& caves = module->caveSystem;

        std::array<int, 3> rootCellSize = {caves.rootCellSize.x, caves.rootCellSize.y,
                                           caves.rootCellSize.z};
        if (ImGui::InputInt3("Root Cell Size", rootCellSize.data())) {
          caves.rootCellSize =
              glm::max(glm::ivec3(rootCellSize[0], rootCellSize[1], rootCellSize[2]),
                       glm::ivec3(4));
          markSelectedModuleDirty("Updated cave root cell size.");
        }

        std::array<int, 3> offset = {caves.offset.x, caves.offset.y, caves.offset.z};
        if (ImGui::InputInt3("Offset", offset.data())) {
          caves.offset = glm::ivec3(offset[0], offset[1], offset[2]);
          markSelectedModuleDirty("Updated cave offset.");
        }

        if (ImGui::SliderInt("Recursion Levels", &caves.recursionLevels, 1, 7)) {
          markSelectedModuleDirty("Updated cave recursion levels.");
        }
        if (ImGui::SliderInt("Subdivision Factor", &caves.subdivisionFactor, 2, 4)) {
          markSelectedModuleDirty("Updated cave subdivision factor.");
        }
        if (ImGui::SliderFloat("Tunnel Radius", &caves.tunnelRadius, 0.5f, 24.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated cave tunnel radius.");
        }
        if (ImGui::SliderFloat("Radius Falloff", &caves.radiusFalloff, 0.15f, 0.95f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated cave radius falloff.");
        }
        if (ImGui::SliderFloat("Chamber Chance", &caves.chamberChance, 0.0f, 1.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated cave chamber chance.");
        }
        if (ImGui::SliderFloat("Chamber Radius Scale", &caves.chamberRadiusScale,
                               0.5f, 4.0f, "%.2f")) {
          markSelectedModuleDirty("Updated cave chamber radius scale.");
        }
        if (ImGui::SliderFloat("Center Jitter", &caves.centerJitter, 0.0f, 0.45f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated cave center jitter.");
        }

        if (ImGui::Checkbox("Infinite Y", &caves.infiniteY)) {
          markSelectedModuleDirty(caves.infiniteY ? "Enabled infinite cave Y."
                                                  : "Disabled infinite cave Y.");
        }
        int minY = caves.minY;
        int maxY = caves.maxY;
        ImGui::BeginDisabled(caves.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          caves.minY = minY;
          if (caves.maxY < caves.minY) {
            caves.maxY = caves.minY;
          }
          markSelectedModuleDirty("Updated cave minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          caves.maxY = std::max(maxY, caves.minY);
          markSelectedModuleDirty("Updated cave maximum height.");
        }
        ImGui::EndDisabled();

        if (ImGui::SliderFloat("Warp Scale", &caves.warpScale, 0.0f, 0.15f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated cave warp scale.");
        }
        if (ImGui::SliderFloat("Warp Strength", &caves.warpStrength, 0.0f, 20.0f,
                               "%.2f")) {
          markSelectedModuleDirty("Updated cave warp strength.");
        }
        if (ImGui::SliderFloat("Accent Noise Scale", &caves.accentNoiseScale, 0.0f,
                               0.25f, "%.3f")) {
          markSelectedModuleDirty("Updated cave accent noise scale.");
        }
        if (ImGui::SliderFloat("Accent Threshold", &caves.accentThreshold, -1.0f,
                               1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated cave accent threshold.");
        }

        drawTricolorPalette(caves.palette);
      } else if (module->type == ModuleType::CELLULAR_NOISE) {
        ImGui::SeparatorText("Cellular Noise");
        CellularNoiseModule& cellular = module->cellularNoise;

        if (ImGui::SliderFloat("Jitter", &cellular.jitter, 0.0f, 1.0f, "%.3f")) {
          markSelectedModuleDirty("Updated cellular jitter.");
        }
        if (ImGui::SliderFloat("Distance Blend", &cellular.distanceBlend, 0.0f, 1.0f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated cellular distance blend.");
        }

        drawVolumeNoiseSettings(cellular.noise);
      } else if (module->type == ModuleType::FRACTAL_NOISE) {
        ImGui::SeparatorText("Fractal Noise");
        FractalNoiseModule& fractal = module->fractalNoise;
        drawVolumeNoiseSettings(fractal.noise);
      } else if (module->type == ModuleType::RIDGED_NOISE) {
        ImGui::SeparatorText("Ridged Noise");
        RidgedNoiseModule& ridged = module->ridgedNoise;

        if (ImGui::SliderFloat("Ridge Sharpness", &ridged.ridgeSharpness, 0.05f,
                               8.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
          ridged.ridgeSharpness = std::max(ridged.ridgeSharpness, 0.05f);
          markSelectedModuleDirty("Updated ridge sharpness.");
        }

        drawVolumeNoiseSettings(ridged.noise);
      } else if (module->type == ModuleType::DOMAIN_WARPED_NOISE) {
        ImGui::SeparatorText("Domain Warped Noise");
        DomainWarpedNoiseModule& warped = module->domainWarpedNoise;

        if (ImGui::SliderInt("Warp Octaves", &warped.warpOctaves, 1, 6)) {
          markSelectedModuleDirty("Updated warp octaves.");
        }
        if (ImGui::SliderFloat("Warp Persistence", &warped.warpPersistence, 0.05f,
                               0.95f, "%.3f")) {
          markSelectedModuleDirty("Updated warp persistence.");
        }
        if (ImGui::SliderFloat("Warp Lacunarity", &warped.warpLacunarity, 1.05f,
                               6.0f, "%.3f")) {
          markSelectedModuleDirty("Updated warp lacunarity.");
        }
        if (ImGui::SliderFloat("Warp Scale", &warped.warpScale, 0.0f, 0.25f,
                               "%.4f")) {
          markSelectedModuleDirty("Updated warp scale.");
        }
        if (ImGui::SliderFloat("Warp Strength", &warped.warpStrength, 0.0f, 24.0f,
                               "%.3f")) {
          markSelectedModuleDirty("Updated warp strength.");
        }

        drawVolumeNoiseSettings(warped.noise);
      } else if (module->type == ModuleType::TREE_GENERATOR) {
        ImGui::SeparatorText("Tree Generator");
        TreeGeneratorModule& tree = module->treeGenerator;

        if (drawBlockTypeMultiSelect("Spawn On Blocks", tree.spawnOnBlocks)) {
          markSelectedModuleDirty("Updated tree support blocks.");
        }

        if (ImGui::BeginCombo("Pattern", voxPlacementPatternDisplayName(tree.pattern))) {
          constexpr VoxPlacementPattern kPatterns[] = {
              VoxPlacementPattern::GRID,
              VoxPlacementPattern::RANDOM_SCATTER,
          };
          for (VoxPlacementPattern candidate : kPatterns) {
            const bool selected = candidate == tree.pattern;
            if (ImGui::Selectable(voxPlacementPatternDisplayName(candidate), selected)) {
              tree.pattern = candidate;
              markSelectedModuleDirty("Updated tree pattern.");
            }
            if (selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        if (ImGui::SliderFloat("Density", &tree.density, 0.0f, 1.0f, "%.2f")) {
          tree.density = std::clamp(tree.density, 0.0f, 1.0f);
          markSelectedModuleDirty("Updated tree density.");
        }

        if (ImGui::BeginCombo("Tree Type",
                              treeGeneratorTypeDisplayName(tree.treeType))) {
          constexpr TreeGeneratorType kTreeTypes[] = {
              TreeGeneratorType::NORMAL,
              TreeGeneratorType::STRANGE,
              TreeGeneratorType::TRUNK_ONLY,
          };
          for (TreeGeneratorType candidate : kTreeTypes) {
            const bool selected = candidate == tree.treeType;
            if (ImGui::Selectable(treeGeneratorTypeDisplayName(candidate), selected)) {
              tree.treeType = candidate;
              markSelectedModuleDirty("Updated tree type.");
            }
            if (selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        if (drawBlockTypeCombo("Trunk Block", tree.trunkBlock)) {
          markSelectedModuleDirty("Updated trunk block.");
        }

        ImGui::BeginDisabled(tree.treeType == TreeGeneratorType::TRUNK_ONLY);
        if (drawBlockTypeCombo("Leaves Block", tree.leavesBlock)) {
          markSelectedModuleDirty("Updated leaves block.");
        }
        ImGui::EndDisabled();

        if (ImGui::Checkbox("Infinite Y", &tree.infiniteY)) {
          markSelectedModuleDirty(tree.infiniteY ? "Enabled infinite tree Y."
                                                 : "Disabled infinite tree Y.");
        }

        int minY = tree.minY;
        int maxY = tree.maxY;
        ImGui::BeginDisabled(tree.infiniteY);
        if (ImGui::InputInt("Min Y", &minY)) {
          tree.minY = minY;
          if (tree.maxY < tree.minY) {
            tree.maxY = tree.minY;
          }
          markSelectedModuleDirty("Updated tree minimum height.");
        }
        if (ImGui::InputInt("Max Y", &maxY)) {
          tree.maxY = std::max(maxY, tree.minY);
          markSelectedModuleDirty("Updated tree maximum height.");
        }
        ImGui::EndDisabled();
      }
    }

  ImGui::End();
}

void BiomeMakerApp::drawStatsWindow() {
  if (!ImGui::Begin("Stats")) {
    ImGui::End();
    return;
  }

  const PreviewWorldController::Stats& stats = previewWorld_.stats();
  ImGui::Text("FPS: %.1f", ENGINE::GETFPS());
  ImGui::Text("CPU: %.2f ms", ENGINE::GETCPUFRAMETIMEMS());
  ImGui::Text("GPU: %.2f ms", ENGINE::GETGPUFRAMETIMEMS());
  ImGui::Separator();
  ImGui::Text("Revision: %llu",
              static_cast<unsigned long long>(stats.generationRevision));
  ImGui::Text("Loaded Chunks: %d", stats.loadedChunks);
  ImGui::Text("Generated Chunks: %d", stats.generatedChunks);
  ImGui::Text("Queued Jobs: %d", stats.queuedGenerations);
  ImGui::Text("Ready Results: %d", stats.lastReadyChunkResults);
  ImGui::Text("Render Distance: %d", stats.streamRenderDistance);
  ImGui::Text("Mode: %s", previewModeDisplayName(stats.mode));
  ImGui::Text("Camera Speed: %.1f", cameraController_.moveSpeed());
  ImGui::Text("Viewport Fog: %s", viewportFogEnabled_ ? "On" : "Off");
  ImGui::Separator();
  ImGui::TextWrapped("Viewport controls: hold RMB over the viewport to fly with "
                     "WASD, Q/E, Shift, mouse look, mouse wheel speed, F focus.");

  ImGui::End();
}

void BiomeMakerApp::drawLogWindow() {
  if (!ImGui::Begin("Log")) {
    ImGui::End();
    return;
  }

  for (const std::string& line : logLines_) {
    ImGui::TextWrapped("%s", line.c_str());
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
    ImGui::SetScrollHereY(1.0f);
  }

  ImGui::End();
}

void BiomeMakerApp::drawViewportWindow(float dtSeconds, float timeSeconds) {
  if (!ImGui::Begin("Viewport")) {
    ImGui::End();
    return;
  }

  ImVec2 available = ImGui::GetContentRegionAvail();
  available.x = std::max(available.x, 1.0f);
  available.y = std::max(available.y, 1.0f);

  const ImVec2 viewportScreenPos = ImGui::GetCursorScreenPos();
  const ImVec2 mousePos = ImGui::GetIO().MousePos;
  const bool hovered =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
      mousePos.x >= viewportScreenPos.x &&
      mousePos.x <= viewportScreenPos.x + available.x &&
      mousePos.y >= viewportScreenPos.y &&
      mousePos.y <= viewportScreenPos.y + available.y;

  const bool wasCapturing = cameraController_.isCapturing();
  cameraController_.update(camera_, dtSeconds, hovered);
  if (!wasCapturing && cameraController_.isCapturing()) {
    ImGui::ClearActiveID();
    ImGui::SetWindowFocus("Viewport");
  }
  if (hovered && Input::keyPressed(GLFW_KEY_F)) {
    cameraController_.focusOnAnchor(camera_, previewWorld_.anchorChunk());
  }

  previewWorld_.update(camera_.position);

  const glm::ivec2 viewportSize(std::max(1, static_cast<int>(available.x)),
                                std::max(1, static_cast<int>(available.y)));

  ENGINE::BEGINGPUFRAMEQUERY();
  renderer_.renderToViewport(
      previewWorld_, camera_, viewportSize,
      ViewportRenderOptions{viewportFogEnabled_}, timeSeconds);
  ENGINE::ENDGPUFRAMEQUERY();

  if (renderer_.colorTexture() != 0) {
    ImGui::Image((ImTextureID)(intptr_t)renderer_.colorTexture(), available,
                 ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
  } else {
    ImGui::TextDisabled("Viewport render target unavailable.");
  }

  if (!hovered) {
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8.0f,
                               ImGui::GetCursorPosY() - available.y + 8.0f));
    ImGui::TextDisabled("Hover + RMB to control the free camera");
  }

  ImGui::End();
}

void BiomeMakerApp::drawOpenPresetPopup() {
  if (openPresetPopupRequested_) {
    ImGui::OpenPopup("Open Preset");
    openPresetPopupRequested_ = false;
  }

  if (!ImGui::BeginPopupModal("Open Preset", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  scanPresetFiles();
  for (const std::filesystem::path& path : presetFiles_) {
    if (ImGui::Selectable(path.filename().string().c_str())) {
      loadPresetDocument(path);
      ImGui::CloseCurrentPopup();
    }
  }
  if (presetFiles_.empty()) {
    ImGui::TextDisabled("No preset files available.");
  }

  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void BiomeMakerApp::drawSaveAsPopup() {
  if (saveAsPopupRequested_) {
    ImGui::OpenPopup("Save Preset As");
    saveAsPopupRequested_ = false;
  }

  if (!ImGui::BeginPopupModal("Save Preset As", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::InputText("Preset Id", saveAsIdBuffer_, sizeof(saveAsIdBuffer_));
  ImGui::InputText("Display Name", saveAsNameBuffer_, sizeof(saveAsNameBuffer_));

  if (ImGui::Button("Save")) {
    currentPreset_.id = sanitizePresetId(saveAsIdBuffer_);
    currentPreset_.displayName = saveAsNameBuffer_;
    const std::filesystem::path path = defaultPathForPreset(currentPreset_);
    if (saveCurrentPresetDocumentAs(path)) {
      syncPresetBuffersFromDocument();
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

bool BiomeMakerApp::drawBlockTypeCombo(const char* label, BlockType& blockType) {
  bool changed = false;
  if (ImGui::BeginCombo(label, getBlockDisplayName(blockType))) {
    for (int index = 1; index < static_cast<int>(BlockType::COUNT); index++) {
      const BlockType candidate = static_cast<BlockType>(index);
      const bool selected = candidate == blockType;
      if (ImGui::Selectable(getBlockDisplayName(candidate), selected)) {
        blockType = candidate;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool BiomeMakerApp::drawBlockTypeMultiSelect(
    const char* label, std::vector<BlockType>& blockTypes) {
  std::string preview = "None";
  if (!blockTypes.empty()) {
    preview.clear();
    for (std::size_t index = 0; index < blockTypes.size(); ++index) {
      if (index > 0) {
        preview += ", ";
      }
      preview += getBlockDisplayName(blockTypes[index]);
      if (index == 2 && blockTypes.size() > 3) {
        preview += ", ...";
        break;
      }
    }
  }

  bool changed = false;
  if (ImGui::BeginCombo(label, preview.c_str())) {
    for (int index = 1; index < static_cast<int>(BlockType::COUNT); ++index) {
      const BlockType candidate = static_cast<BlockType>(index);
      const auto iterator =
          std::find(blockTypes.begin(), blockTypes.end(), candidate);
      const bool selected = iterator != blockTypes.end();
      if (ImGui::Selectable(getBlockDisplayName(candidate), selected,
                            ImGuiSelectableFlags_DontClosePopups)) {
        if (selected) {
          if (blockTypes.size() > 1) {
            blockTypes.erase(iterator);
            changed = true;
          }
        } else {
          blockTypes.push_back(candidate);
          std::sort(blockTypes.begin(), blockTypes.end(),
                    [](BlockType left, BlockType right) {
                      return static_cast<int>(left) < static_cast<int>(right);
                    });
          changed = true;
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  return changed;
}

} // namespace BiomeMaker
