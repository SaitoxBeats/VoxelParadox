#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "engine/camera.hpp"
#include "engine/shader.hpp"
#include "world/block.hpp"

namespace ShaderEditor {

struct PreviewRenderSettings {
  glm::vec4 backgroundColor{0.08f, 0.09f, 0.12f, 1.0f};
  glm::vec4 biomeTint{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec3 breakBlockCenter{0.5f, 0.5f, 0.5f};
  BlockId blockType = BlockIds::STONE;
  float breakState = 0.0f;
  bool highlightEnabled = false;
  bool wireframe = false;
};

class ShaderEditorRenderer {
public:
  bool init();
  void cleanup();

  void render(const Shader* activeShader, const Camera& camera,
              const glm::ivec2& viewportSize,
              const PreviewRenderSettings& settings, float timeSeconds);

  GLuint colorTexture() const { return colorTexture_; }
  const glm::ivec2& viewportSize() const { return viewportSize_; }

private:
  struct PreviewCubeVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tint;
    float ao;
    float material;
  };

  Shader fallbackShader_{};
  GLuint blockAtlasTexture_ = 0;
  GLuint framebuffer_ = 0;
  GLuint colorTexture_ = 0;
  GLuint depthStencilRenderbuffer_ = 0;
  GLuint cubeVao_ = 0;
  GLuint cubeVbo_ = 0;
  glm::ivec2 viewportSize_{0};
  BlockId currentBlockType_ = BlockIds::AIR;

  bool setupBlockAtlasTexture();
  void cleanupBlockAtlasTexture();
  void bindBlockAtlasTexture(const Shader& shader);
  void destroyFramebuffer();
  bool ensureFramebuffer(const glm::ivec2& size);
  bool ensureCubeGeometry();
  void updateCubeGeometry(BlockId blockType);
};

} // namespace ShaderEditor
