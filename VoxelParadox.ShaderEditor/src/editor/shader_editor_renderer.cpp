#include "shader_editor_renderer.hpp"

// 1. Standard Library
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

// 2. Local Project Modules
#include "path/app_paths.hpp"
#include "render/cache/block_texture_cache.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "world/block/block_registry.hpp"

namespace ShaderEditor {
namespace {

constexpr const char* kFallbackVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTint;
layout(location = 3) in float aAO;
layout(location = 4) in float aMaterial;

uniform mat4 uVP;
uniform mat4 uModel;

out vec3 vNormal;

void main() {
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uVP * uModel * vec4(aPos, 1.0);
}
)";

constexpr const char* kFallbackFragmentShader = R"(
#version 330 core
in vec3 vNormal;
out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    float lighting = 0.45 + max(dot(normal, normalize(vec3(0.35, 1.0, 0.25))), 0.0) * 0.55;
    FragColor = vec4(vec3(1.0, 0.0, 1.0) * lighting, 1.0);
}
)";

} // namespace

bool ShaderEditorRenderer::init() {
  if (!fallbackShader_.compile(kFallbackVertexShader, kFallbackFragmentShader)) {
    return false;
  }

  if (!ensureCubeGeometry()) {
    return false;
  }

  return setupBlockTextures();
}

void ShaderEditorRenderer::cleanup() {
  if (cubeVao_ != 0) {
    glDeleteVertexArrays(1, &cubeVao_);
    cubeVao_ = 0;
  }
  if (cubeVbo_ != 0) {
    glDeleteBuffers(1, &cubeVbo_);
    cubeVbo_ = 0;
  }
  if (worldVao_ != 0) {
    glDeleteVertexArrays(1, &worldVao_);
    worldVao_ = 0;
  }
  if (worldVbo_ != 0) {
    glDeleteBuffers(1, &worldVbo_);
    worldVbo_ = 0;
  }

  destroyFramebuffer();
  cleanupBlockTextures();
  fallbackShader_.release();
}

void ShaderEditorRenderer::render(const Shader* activeShader, const Camera& camera,
                                  const glm::ivec2& viewportSize,
                                  const PreviewRenderSettings& settings,
                                  float timeSeconds) {
  if (viewportSize.x <= 0 || viewportSize.y <= 0) {
    return;
  }
  if (!ensureFramebuffer(viewportSize)) {
    return;
  }
  if (!ensureBlockTexturesUpToDate()) {
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, viewportSize_.x, viewportSize_.y);
  glClearColor(settings.backgroundColor.r, settings.backgroundColor.g,
               settings.backgroundColor.b, settings.backgroundColor.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  const float aspect = static_cast<float>(viewportSize_.x) /
                       static_cast<float>(viewportSize_.y);
  const glm::mat4 view = camera.getViewMatrix();
  const glm::mat4 projection = camera.getProjectionMatrix(aspect);
  const glm::mat4 viewProjection = projection * view;

  const Shader& shader =
      (activeShader && activeShader->program != 0) ? *activeShader : fallbackShader_;

  shader.use();
  shader.setMat4("uVP", viewProjection);
  shader.setMat4("uModel", glm::mat4(1.0f));
  shader.setVec3("uCameraPos", camera.position);
  shader.setVec4("uFogColor", settings.backgroundColor);
  shader.setFloat("uFogDensity", 0.0f);
  shader.setFloat("uTime", timeSeconds);
  shader.setFloat("uAlpha", 1.0f);
  shader.setFloat("uAoStrength", 1.0f);
  shader.setVec4("uBiomeTint", settings.biomeTint);
  shader.setFloat("uBreakProgress", settings.breakState);
  shader.setFloat("uHighlightActive", settings.highlightEnabled ? 1.0f : 0.0f);
  bindBlockTextures();

  glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

  if (settings.previewMode == PreviewMode::WORLD_CLUSTER) {
    if (!ensureWorldClusterGeometry()) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return;
    }
    if (settings.blockType != currentWorldBlockType_) {
      updateWorldClusterGeometry(settings.blockType);
    }
    // World-space positions: the shader receives actual worldPos coordinates so
    // seamless UV projections and cross-block effects behave exactly as in-game.
    shader.setInt("uUseLocalMaterialSpace", 0);
    // Center the break/highlight anchor on the middle block of the 3x3 cluster.
    shader.setVec3("uBreakBlockCenter", glm::vec3(1.5f, 0.5f, 1.5f));
    shader.setVec3("uHighlightBlockCenter", glm::vec3(1.5f, 0.5f, 1.5f));
    glBindVertexArray(worldVao_);
    glDrawArrays(GL_TRIANGLES, 0, worldClusterVertexCount_);
    glBindVertexArray(0);
  } else {
    if (!ensureCubeGeometry()) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return;
    }
    if (settings.blockType != currentBlockType_) {
      updateCubeGeometry(settings.blockType);
    }
    shader.setInt("uUseLocalMaterialSpace", 1);
    shader.setVec3("uBreakBlockCenter", settings.breakBlockCenter);
    shader.setVec3("uHighlightBlockCenter", settings.breakBlockCenter);
    glBindVertexArray(cubeVao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool ShaderEditorRenderer::setupBlockTextures() {
  cleanupBlockTextures();
  blockTextures_ = BlockTextureCache::loadBlockTextures(
      BlockRegistry::instance().definitions(),
      &loadedBlockTexturePaths_,
      &loadedBlockTextureWriteTimes_
  );
  return true;
}

void ShaderEditorRenderer::cleanupBlockTextures() {
  BlockTextureCache::destroyBlockTextures(blockTextures_);
  loadedBlockTexturePaths_.clear();
  loadedBlockTextureWriteTimes_.clear();
}

bool ShaderEditorRenderer::ensureBlockTexturesUpToDate() {
  const std::vector<BlockDefinition>& definitions = BlockRegistry::instance().definitions();

  std::vector<std::filesystem::path> expectedTexturePaths;
  std::vector<std::filesystem::file_time_type> expectedWriteTimes;
  expectedTexturePaths.reserve(definitions.size());
  expectedWriteTimes.reserve(definitions.size());

  for (const BlockDefinition& definition : definitions) {
    if (definition.textureAssetPath.empty()) {
      continue;
    }

    const std::filesystem::path resolvedPath =
        AppPaths::resolve(definition.textureAssetPath);
    std::error_code ec;
    const bool exists = std::filesystem::exists(resolvedPath, ec);
    const std::filesystem::file_time_type writeTime =
        (!ec && exists) ? std::filesystem::last_write_time(resolvedPath, ec)
                        : std::filesystem::file_time_type{};

    expectedTexturePaths.push_back(resolvedPath);
    expectedWriteTimes.push_back(writeTime);
  }

  const bool needsReload =
      blockTextures_.size() != expectedTexturePaths.size() ||
      loadedBlockTexturePaths_ != expectedTexturePaths ||
      loadedBlockTextureWriteTimes_ != expectedWriteTimes;

  if (needsReload) {
    return setupBlockTextures();
  }

  return true;
}

void ShaderEditorRenderer::bindBlockTextures() {
  BlockTextureCache::bindBlockTextures(blockTextures_);
}

void ShaderEditorRenderer::destroyFramebuffer() {
  if (depthStencilRenderbuffer_ != 0) {
    glDeleteRenderbuffers(1, &depthStencilRenderbuffer_);
    depthStencilRenderbuffer_ = 0;
  }
  if (colorTexture_ != 0) {
    glDeleteTextures(1, &colorTexture_);
    colorTexture_ = 0;
  }
  if (framebuffer_ != 0) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }

  viewportSize_ = glm::ivec2(0);
}

bool ShaderEditorRenderer::ensureFramebuffer(const glm::ivec2& size) {
  if (framebuffer_ != 0 && viewportSize_ == size && colorTexture_ != 0 &&
      depthStencilRenderbuffer_ != 0) {
    return true;
  }

  destroyFramebuffer();
  viewportSize_ = size;

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

  glGenTextures(1, &colorTexture_);
  glBindTexture(GL_TEXTURE_2D, colorTexture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewportSize_.x, viewportSize_.y, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         colorTexture_, 0);

  glGenRenderbuffers(1, &depthStencilRenderbuffer_);
  glBindRenderbuffer(GL_RENDERBUFFER, depthStencilRenderbuffer_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportSize_.x,
                        viewportSize_.y);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, depthStencilRenderbuffer_);

  const bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return complete;
}

bool ShaderEditorRenderer::ensureCubeGeometry() {
  if (cubeVao_ != 0 && cubeVbo_ != 0) {
    return true;
  }

  glGenVertexArrays(1, &cubeVao_);
  glGenBuffers(1, &cubeVbo_);
  glBindVertexArray(cubeVao_);
  glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(PreviewCubeVertex) * 36, nullptr,
               GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, position)));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, normal)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, tint)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, ao)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, material)));
  glEnableVertexAttribArray(4);
  glBindVertexArray(0);

  updateCubeGeometry(BlockIds::STONE);
  return true;
}

void ShaderEditorRenderer::updateCubeGeometry(BlockId blockType) {
  static const glm::vec3 normals[6] = {
      {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
  };
  static const glm::vec3 faceVertices[6][6] = {
      {{0, 0, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
      {{1, 0, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
      {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 0}, {1, 0, 1}, {0, 0, 1}},
      {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 0}, {0, 1, 0}},
      {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
      {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 1, 1}},
  };

  std::array<PreviewCubeVertex, 36> vertices{};
  std::size_t vertexIndex = 0;

  for (int face = 0; face < 6; face++) {
    const glm::vec4 tint = getBlockColor(blockType, 0, face);
    const float material = getBlockMaterialId(blockType);
    for (int i = 0; i < 6; i++) {
      vertices[vertexIndex++] = {faceVertices[face][i], normals[face], tint, 1.0f,
                                 material};
    }
  }

  glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
  currentBlockType_ = blockType;
}

bool ShaderEditorRenderer::ensureWorldClusterGeometry() {
  if (worldVao_ != 0 && worldVbo_ != 0) {
    return true;
  }

  glGenVertexArrays(1, &worldVao_);
  glGenBuffers(1, &worldVbo_);
  glBindVertexArray(worldVao_);
  glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(PreviewCubeVertex) * kWorldClusterVertexCount,
               nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, position)));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, normal)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, tint)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, ao)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PreviewCubeVertex),
                        reinterpret_cast<void*>(offsetof(PreviewCubeVertex, material)));
  glEnableVertexAttribArray(4);
  glBindVertexArray(0);

  updateWorldClusterGeometry(BlockIds::STONE);
  return true;
}

void ShaderEditorRenderer::updateWorldClusterGeometry(BlockId blockType) {
  static const glm::vec3 normals[6] = {
      {-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f},
      {0.f, -1.f, 0.f}, {0.f, 1.f, 0.f},
      {0.f, 0.f, -1.f}, {0.f, 0.f, 1.f},
  };
  static const glm::vec3 faceVertices[6][6] = {
      {{0, 0, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
      {{1, 0, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
      {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 0}, {1, 0, 1}, {0, 0, 1}},
      {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 0}, {0, 1, 0}},
      {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
      {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 1, 1}},
  };

  const float material = getBlockMaterialId(blockType);

  // Only generate faces that would be rendered in-game. Interior side faces
  // (shared between adjacent same-type blocks) are culled by the chunk mesher
  // and must be omitted here so block boundaries don't produce visible walls
  // that the user would mistake for UV seams.
  //   face 0 = -X: only at the left  boundary (gx == 0)
  //   face 1 = +X: only at the right boundary (gx == 2)
  //   face 2 = -Y: always (bottom)
  //   face 3 = +Y: always (top)
  //   face 4 = -Z: only at the front boundary (gz == 0)
  //   face 5 = +Z: only at the back  boundary (gz == 2)
  std::array<PreviewCubeVertex, kWorldClusterVertexCount> vertices{};
  std::size_t vi = 0;

  for (int gz = 0; gz < 3; ++gz) {
    for (int gx = 0; gx < 3; ++gx) {
      // Block (gx, gz) sits at world position [gx, gx+1] x [0,1] x [gz, gz+1].
      const glm::vec3 blockOffset(static_cast<float>(gx), 0.f,
                                  static_cast<float>(gz));
      for (int face = 0; face < 6; ++face) {
        if (face == 0 && gx != 0) continue;  // -X interior
        if (face == 1 && gx != 2) continue;  // +X interior
        if (face == 4 && gz != 0) continue;  // -Z interior
        if (face == 5 && gz != 2) continue;  // +Z interior

        const glm::vec4 tint = getBlockColor(blockType, 0, face);
        for (int i = 0; i < 6; ++i) {
          vertices[vi++] = {faceVertices[face][i] + blockOffset,
                            normals[face], tint, 1.0f, material};
        }
      }
    }
  }

  worldClusterVertexCount_ = static_cast<int>(vi);
  glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(sizeof(PreviewCubeVertex) * vi),
                  vertices.data());
  currentWorldBlockType_ = blockType;
}

} // namespace ShaderEditor
