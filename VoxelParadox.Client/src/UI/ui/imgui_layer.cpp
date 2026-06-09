// Arquivo: VoxelParadox.Client/src/UI/ui/imgui_layer.cpp
// Papel: implementa "imgui layer" dentro do subsistema "ui" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "imgui_layer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#pragma endregion

#pragma region ImguiLayerImplementation
namespace ImGuiLayer {
namespace {

constexpr const char* kGlslVersion = "#version 330";
bool initialized = false;

} // namespace

// Funcao: inicializa 'initialize' na camada ImGui do runtime.
// Detalhe: usa 'window' para preparar os recursos e o estado inicial antes do uso.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool initialize(GLFWwindow* window) {
  if (initialized) {
    return true;
  }
  if (!window) {
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
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

// Funcao: inicia 'beginFrame' na camada ImGui do runtime.
// Detalhe: centraliza a logica necessaria para marcar o inicio de uma transicao ou fluxo controlado.
void beginFrame() {
  if (!initialized) {
    return;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

// Funcao: renderiza 'render' na camada ImGui do runtime.
// Detalhe: centraliza a logica necessaria para desenhar a saida visual correspondente usando o estado atual.
void render() {
  if (!initialized) {
    return;
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Funcao: libera 'shutdown' na camada ImGui do runtime.
// Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
void shutdown() {
  if (!initialized) {
    return;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  initialized = false;
}

} // namespace ImGuiLayer
#pragma endregion
