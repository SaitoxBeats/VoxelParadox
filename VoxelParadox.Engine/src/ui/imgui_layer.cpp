// Arquivo: Engine/src/ui/imgui_layer.cpp
// Papel: implementa a integraÃ§Ã£o compartilhada de ImGui do engine.
// Fluxo: materializa a ponte entre contexto ImGui e backends GLFW/OpenGL do projeto.
// DependÃªncias principais: ImGui, GLFW e OpenGL.
#include "imgui_layer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace ImGuiLayer {
namespace {

constexpr const char* kGlslVersion = "#version 330";
bool initialized = false;
std::string iniFilenameStorage;

} // namespace

bool initialize(GLFWwindow* window, const Config& config) {
  if (initialized) {
    return true;
  }
  if (!window) {
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  if (config.dockingEnabled) {
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
#endif
  }
  iniFilenameStorage = config.iniFilename;
  io.IniFilename = iniFilenameStorage.empty() ? nullptr : iniFilenameStorage.c_str();
  ImGui::StyleColorsDark();

  if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
    ImGui::DestroyContext();
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  initialized = true;
  return true;
}

void beginFrame() {
  if (!initialized) {
    return;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void render() {
  if (!initialized) {
    return;
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void shutdown() {
  if (!initialized) {
    return;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  iniFilenameStorage.clear();
  initialized = false;
}

} // namespace ImGuiLayer
