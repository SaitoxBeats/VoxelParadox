// Arquivo: Engine/src/engine/bootstrap.h
// Papel: declara um componente do nÃºcleo compartilhado do engine.
// Fluxo: apoia bootstrap, timing, input, cÃ¢mera ou render base do projeto.
// DependÃªncias principais: GLFW, GLM e utilitÃ¡rios do engine.
#pragma once

// Arquivo: Engine/src/engine/bootstrap.h
// Papel: declara o bootstrap de janela, OpenGL, assets crÃƒÂ­ticos e diretÃƒÂ³rio de save.
// Fluxo: o runtime e o BiomeMaker montam uma `Bootstrap::Config` e chamam `initialize`
// antes de instanciar renderer, HUD ou ferramentas de editor.
// DependÃƒÂªncias principais: `ENGINE`, `AppPaths`, GLFW e OpenGL.

#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "path/app_paths.hpp"
#include "engine.hpp"

struct GLFWwindow;

namespace Bootstrap {

struct Config {
  int openGLMajor = 3;
  int openGLMinor = 3;
  std::string windowTitle = "Voxel Paradox";
  glm::ivec2 windowSize{1280, 720};
  ENGINE::VIEWPORTMODE viewportMode = ENGINE::VIEWPORTMODE::BORDERLESS;
  std::string defaultFontPath = "Assets/Fonts/zpix.ttf";
  bool vSyncEnabled = false;
  std::string saveDirectory = "Saves/worlds";
  std::vector<std::string> requiredAssetFiles{
      "Assets/Textures/Hud/crosshair.png",
      "Assets/Textures/Items/axe.png",
      "Assets/Voxs/castle.vox",
      "Assets/Recipes/recipe_blocks.json",
      "Assets/Recipes/recipe_tools.json",
      "Assets/Shaders/block.vert",
      "Assets/Shaders/block.frag",
      "Assets/Shaders/line.vert",
      "Assets/Shaders/line.frag",
      "Assets/Shaders/depth_window.vert",
      "Assets/Shaders/depth_window.frag",
      "Assets/Shaders/dust_particle.vert",
      "Assets/Shaders/dust_particle.frag",
      "Assets/Shaders/item_sprite.vert",
      "Assets/Shaders/item_sprite.frag",
      "Assets/Shaders/hud.vert",
      "Assets/Shaders/hud.frag",
  };
  std::vector<std::string> requiredAssetDirectories{
      "Assets",
      "Assets/Fonts",
      "Assets/Textures",
      "Assets/Textures/Hud",
      "Assets/Textures/Items",
      "Assets/Recipes",
      "Assets/Shaders",
      "Assets/Voxs",
      "Assets/Voxs/ruins",
  };
};

bool initialize(const Config& config, GLFWwindow*& outWindow);
void reportImGuiStatus(bool initialized);
void shutdownWindow(GLFWwindow* window);
const char* viewportModeName(ENGINE::VIEWPORTMODE mode);

} // namespace Bootstrap
