#pragma once

#include <filesystem>

#include <GLFW/glfw3.h>

#include "engine/bootstrap.hpp"
#include "engine/camera.hpp"

#include "block_shader_session.hpp"
#include "orbit_camera_controller.hpp"
#include "shader_editor_renderer.hpp"

namespace ShaderEditor {

class ShaderEditorApp {
public:
  bool run();

private:
  GLFWwindow* window_ = nullptr;
  Bootstrap::Config bootstrapConfig_{};
  ShaderEditorRenderer renderer_{};
  BlockShaderSession shaderSession_{};
  OrbitCameraController orbitCamera_{};
  Camera camera_{};
  PreviewRenderSettings previewSettings_{};
  double currentTimeSeconds_ = 0.0;

  bool initialize();
  void shutdown();
  std::filesystem::path editorIniPath() const;

  void drawUi(float dtSeconds, float timeSeconds);
  void drawDockspace();
  void drawStatusPanel();
  void drawPreviewWindow(float dtSeconds, float timeSeconds);
  void drawControlsWindow();
  void drawDiagnosticsWindow();

  bool drawBlockIdCombo(const char* label, BlockId& blockId);
};

} // namespace ShaderEditor
