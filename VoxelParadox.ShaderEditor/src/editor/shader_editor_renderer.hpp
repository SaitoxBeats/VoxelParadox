#pragma once

// 1. Standard Library
#include <filesystem>
#include <string>
#include <vector>

// 2. External Libraries
#include <glad/glad.h>
#include <glm/glm.hpp>

// 3. Project Headers
#include "engine/camera.hpp"
#include "engine/shader.hpp"
#include "world/block/block.hpp"

namespace ShaderEditor {

enum class PreviewMode {
  SINGLE_BLOCK,   // One block orbited by the camera.
  WORLD_CLUSTER,  // 3x3 flat ground layer; shows cross-block seamless behaviour.
};

struct PreviewRenderSettings {
  glm::vec4 backgroundColor{0.08f, 0.09f, 0.12f, 1.0f};
  glm::vec4 biomeTint{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec3 breakBlockCenter{0.5f, 0.5f, 0.5f};
  BlockId blockType = BlockIds::STONE;
  float breakState = 0.0f;
  bool highlightEnabled = false;
  bool wireframe = false;
  PreviewMode previewMode = PreviewMode::SINGLE_BLOCK;
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
  std::vector<GLuint> blockTextures_{};
  GLuint framebuffer_ = 0;
  GLuint colorTexture_ = 0;
  GLuint depthStencilRenderbuffer_ = 0;
  GLuint cubeVao_ = 0;
  GLuint cubeVbo_ = 0;
  GLuint worldVao_ = 0;
  GLuint worldVbo_ = 0;
  // Buffer sized for worst case (all faces). Actual draw count is tracked separately.
  static constexpr int kWorldClusterVertexCount = 9 * 36;  // 3x3 blocks x 36 verts max
  int worldClusterVertexCount_ = 0;
  glm::ivec2 viewportSize_{0};
  BlockId currentBlockType_ = BlockIds::AIR;
  BlockId currentWorldBlockType_ = BlockIds::AIR;
  std::vector<std::filesystem::path> loadedBlockTexturePaths_{};
  std::vector<std::filesystem::file_time_type> loadedBlockTextureWriteTimes_{};

  bool setupBlockTextures();
  bool ensureBlockTexturesUpToDate();
  void cleanupBlockTextures();
  void bindBlockTextures();
  void destroyFramebuffer();
  bool ensureFramebuffer(const glm::ivec2& size);
  bool ensureCubeGeometry();
  void updateCubeGeometry(BlockId blockType);
  bool ensureWorldClusterGeometry();
  void updateWorldClusterGeometry(BlockId blockType);
};

} // namespace ShaderEditor
