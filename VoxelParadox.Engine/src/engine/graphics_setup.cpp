// Arquivo: Engine/src/engine/graphics_setup.cpp
// Papel: implementa a inicializaÃ§Ã£o mÃ­nima de GLFW, GLAD e estado base de OpenGL.
// Fluxo: executa a sequÃªncia enxuta usada pelo bootstrap antes de o runtime subir o renderer.
// DependÃªncias principais: GLFW, GLAD e OpenGL.
#include "graphics_setup.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace GraphicsSetup {

bool initializeGLFW() {
  return glfwInit() == GLFW_TRUE;
}

bool initializeGlad() {
  return gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0;
}

void configureOpenGLState() {
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_MULTISAMPLE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
}

} // namespace GraphicsSetup
