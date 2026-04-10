#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "engine/camera.hpp"
#include "engine/shader.hpp"

#include "preview_world_controller.hpp"

namespace BiomeMaker {

struct ViewportRenderOptions {
  bool fogEnabled = true;
};

class EditorRenderer {
public:
  bool init();
  void cleanup();

  void renderToViewport(PreviewWorldController& world, const Camera& camera,
                        const glm::ivec2& viewportSize,
                        const ViewportRenderOptions& options, float timeSeconds);

  GLuint colorTexture() const { return colorTexture_; }
  const glm::ivec2& viewportSize() const { return viewportSize_; }

private:
  Shader blockShader_;
  GLuint blockAtlasTexture_ = 0;
  GLuint framebuffer_ = 0;
  GLuint colorTexture_ = 0;
  GLuint depthStencilRenderbuffer_ = 0;
  glm::ivec2 viewportSize_{0};

  bool setupBlockAtlasTexture();
  void cleanupBlockAtlasTexture();
  void bindBlockAtlasTexture();
  void destroyFramebuffer();
  bool ensureFramebuffer(const glm::ivec2& size);
  glm::vec4 getFogColor(int depth) const;
  float computeFogDensity(int depth, int renderDistance) const;
};

} // namespace BiomeMaker
