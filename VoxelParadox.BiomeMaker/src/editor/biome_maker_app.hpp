#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "engine/bootstrap.hpp"
#include "engine/camera.hpp"
#include "world/biome_preset.hpp"

#include "editor_camera_controller.hpp"
#include "editor_renderer.hpp"
#include "preview_world_controller.hpp"

namespace BiomeMaker {

using namespace VoxelGame;

class BiomeMakerApp {
public:
  bool run();

private:
  struct ModuleLibraryEntry {
    std::filesystem::path absolutePath{};
    std::filesystem::path relativePath{};
    BiomeModule module{};
  };

  GLFWwindow* window_ = nullptr;
  Bootstrap::Config bootstrapConfig_{};
  EditorRenderer renderer_{};
  PreviewWorldController previewWorld_{};
  EditorCameraController cameraController_{};
  Camera camera_{};

  BiomePreset currentPreset_{};
  std::filesystem::path currentPresetPath_{};
  bool documentDirty_ = false;
  bool previewSourceDirty_ = true;

  std::vector<std::filesystem::path> presetFiles_{};
  std::vector<ModuleLibraryEntry> moduleLibrary_{};
  std::vector<std::string> logLines_{};

  bool openPresetPopupRequested_ = false;
  bool saveAsPopupRequested_ = false;
  bool dockLayoutInitialized_ = false;
  bool viewportFogEnabled_ = true;
  int selectedLibraryModuleIndex_ = 0;
  int selectedModuleIndex_ = -1;
  int bufferedModuleIndex_ = -1;

  char presetIdBuffer_[128]{};
  char presetNameBuffer_[128]{};
  char saveAsIdBuffer_[128]{};
  char saveAsNameBuffer_[128]{};
  char moduleIdBuffer_[128]{};
  char moduleNameBuffer_[128]{};
  char moduleSourceDirBuffer_[256]{};

  bool initialize();
  void shutdown();
  void ensureWorkspaceFolders();
  void ensureDefaultModuleLibrary();
  void ensureDefaultPresetExists();
  void scanPresetFiles();
  void scanModuleLibrary();
  void appendLog(const std::string& message);
  void syncPresetBuffersFromDocument();
  void openSaveAsPopup();
  void markDocumentDirty(const char* reason);
  void syncPreviewSource();
  void handleShortcuts();

  void newPresetDocument();
  void duplicatePresetDocument();
  bool loadPresetDocument(const std::filesystem::path& path);
  bool saveCurrentPresetDocument();
  bool saveCurrentPresetDocumentAs(const std::filesystem::path& path);
  bool revertCurrentPresetDocument();

  std::filesystem::path defaultPathForPreset(const BiomePreset& preset) const;
  std::filesystem::path presetDirectory() const;
  std::filesystem::path resolvePresetRelativePath(
      const std::filesystem::path& relativePath) const;
  std::filesystem::path makeUniqueModuleRelativePath(
      const std::string& baseName,
      const std::filesystem::path& ignoreRelativePath = {}) const;
  bool isSharedLibraryModulePath(const std::filesystem::path& path) const;
  void clampSelectedModuleIndex();
  BiomeModule* selectedModule();
  const BiomeModule* selectedModule() const;
  void syncSelectedModuleBuffers();
  void markSelectedModuleDirty(const char* reason);
  std::string makeUniqueModuleId(const std::string& base,
                                 int ignoreIndex = -1) const;
  void addModuleFromLibrary(int libraryIndex);
  void duplicateSelectedModule();
  void removeSelectedModule();
  void moveSelectedModule(int direction);
  int countImportVoxelFiles(const ImportVoxFilesModule& module) const;

  void drawUi(float dtSeconds, float timeSeconds);
  void drawDockspace();
  void drawMenuBar();
  void drawSourceBrowser();
  void drawInspector();
  void drawStatsWindow();
  void drawLogWindow();
  void drawViewportWindow(float dtSeconds, float timeSeconds);
  void drawOpenPresetPopup();
  void drawSaveAsPopup();

  bool drawBlockIdCombo(const char* label, BlockId& blockId);
  bool drawBlockIdMultiSelect(const char* label,
                              std::vector<BlockId>& blockIds);
};

} // namespace BiomeMaker
