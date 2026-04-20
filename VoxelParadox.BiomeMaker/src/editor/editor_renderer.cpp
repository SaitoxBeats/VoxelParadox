#include "editor_renderer.hpp"

// 1. Standard Library
#include <algorithm>

// 2. Local Project Modules
#include "engine/engine.hpp"
#include "render/cache/block_texture_cache.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "world/block/block_registry.hpp"

namespace BiomeMaker {

bool EditorRenderer::init() {
  const BlockShaderSources blockShaderSources =
      BlockRegistry::instance().buildShaderSources();
  const bool compiled =
      blockShaderSources.valid() &&
      blockShader_.compile(
          blockShaderSources.vertexSource.c_str(),
          blockShaderSources.fragmentSource.c_str()
      );

  if (!compiled) {
    return false;
  }

  return setupBlockTextures();
}

void EditorRenderer::destroyFramebuffer() {
  releaseCloudDepthTexture();

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

void EditorRenderer::cleanup() {
  destroyFramebuffer();
  cleanupBlockTextures();
  cloudRenderer_.cleanup();
  blockShader_.release();
}

void EditorRenderer::renderToViewport(PreviewWorldController& world, const Camera& camera,
                                      const glm::ivec2& viewportSize,
                                      const ViewportRenderOptions& options,
                                      float timeSeconds) {
  if (viewportSize.x <= 0 || viewportSize.y <= 0) {
    return;
  }
  if (!ensureFramebuffer(viewportSize)) {
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, viewportSize_.x, viewportSize_.y);

  const int depth = world.depth();
  const glm::vec4 fogColor = getFogColor(depth);
  const glm::vec4 clearColor =
      options.fogEnabled ? fogColor : glm::vec4(0.015f, 0.015f, 0.02f, 1.0f);
  glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  const float aspect = static_cast<float>(viewportSize_.x) /
                       static_cast<float>(viewportSize_.y);
  const glm::mat4 view = camera.getViewMatrix();
  const glm::mat4 projection = camera.getProjectionMatrix(aspect);
  const glm::mat4 viewProjection = projection * view;

  blockShader_.use();
  blockShader_.setMat4("uVP", viewProjection);
  blockShader_.setMat4("uModel", glm::mat4(1.0f));
  blockShader_.setVec3("uCameraPos", camera.position);
  blockShader_.setVec4("uFogColor", fogColor);
  blockShader_.setFloat(
      "uFogDensity",
      options.fogEnabled ? computeFogDensity(depth, world.previewRenderDistance())
                         : 0.0f);
  blockShader_.setFloat("uTime", timeSeconds);
  blockShader_.setFloat("uAlpha", 1.0f);
  blockShader_.setFloat("uAoStrength", 1.0f);
  blockShader_.setVec4("uBiomeTint", glm::vec4(1.0f));
  blockShader_.setInt("uUseLocalMaterialSpace", 0);
  blockShader_.setVec3("uBreakBlockCenter", glm::vec3(0.0f));
  blockShader_.setFloat("uBreakProgress", 0.0f);
  blockShader_.setVec3("uHighlightBlockCenter", glm::vec3(0.0f));
  blockShader_.setFloat("uHighlightActive", 0.0f);
  bindBlockTextures();

  world.render(camera.position, viewProjection);

  if (const BiomePreset* preset = world.biomePreset()) {
    const int depthTextureUnit = cloudDepthTextureUnit();
    const bool hasDepthTexture =
        depthTextureUnit >= 0 && captureCloudDepthTexture();

    bindBlockTextures();
    VoxelGame::CloudRenderContext cloudContext{};
    cloudContext.preset = preset;
    cloudContext.seed = world.seed();
    cloudContext.depth = depth;
    cloudContext.cameraPosition = camera.position;
    cloudContext.viewProjection = viewProjection;
    cloudContext.fogColor = fogColor;
    cloudContext.fogDensity =
        options.fogEnabled ? computeFogDensity(depth, world.previewRenderDistance())
                           : 0.0f;
    cloudContext.timeSeconds = timeSeconds;
    cloudContext.fallbackRenderDistance = world.previewRenderDistance();
    cloudContext.alphaMultiplier = 1.0f;
    cloudContext.quality = VoxelGame::Clouds::CloudQuality::HIGH;
    cloudContext.sceneDepthTexture = hasDepthTexture ? cloudSceneDepthTexture_ : 0;
    cloudContext.sceneDepthTextureUnit = depthTextureUnit;
    cloudContext.viewportSize = cloudSceneDepthTextureSize_;
    cloudContext.inverseProjection = glm::inverse(projection);
    cloudRenderer_.render(cloudContext, blockShader_);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool EditorRenderer::ensureFramebuffer(const glm::ivec2& size) {
  if (size == viewportSize_ && framebuffer_ != 0 && colorTexture_ != 0 &&
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

bool EditorRenderer::setupBlockTextures() {
  cleanupBlockTextures();
  blockTextures_ =
      BlockTextureCache::loadBlockTextures(BlockRegistry::instance().definitions());
  return true;
}

void EditorRenderer::cleanupBlockTextures() {
  BlockTextureCache::destroyBlockTextures(blockTextures_);
}

void EditorRenderer::bindBlockTextures() {
  BlockTextureCache::bindBlockTextures(blockTextures_);
}

bool EditorRenderer::captureCloudDepthTexture() {
  if (viewportSize_.x <= 0 || viewportSize_.y <= 0) {
    return false;
  }

  const int textureUnit = cloudDepthTextureUnit();
  if (textureUnit < 0) {
    return false;
  }

  GLint previousActiveTexture = GL_TEXTURE0;
  GLint previousTexture = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(textureUnit));
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

  if (cloudSceneDepthTexture_ == 0 ||
      cloudSceneDepthTextureSize_ != viewportSize_) {
    if (previousTexture == static_cast<GLint>(cloudSceneDepthTexture_)) {
      previousTexture = 0;
    }

    releaseCloudDepthTexture();

    glGenTextures(1, &cloudSceneDepthTexture_);
    glBindTexture(GL_TEXTURE_2D, cloudSceneDepthTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, viewportSize_.x,
                 viewportSize_.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    cloudSceneDepthTextureSize_ = viewportSize_;
  } else {
    glBindTexture(GL_TEXTURE_2D, cloudSceneDepthTexture_);
  }

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, viewportSize_.x,
                      viewportSize_.y);

  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
  glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  return true;
}

void EditorRenderer::releaseCloudDepthTexture() {
  if (cloudSceneDepthTexture_ != 0) {
    glDeleteTextures(1, &cloudSceneDepthTexture_);
    cloudSceneDepthTexture_ = 0;
  }

  cloudSceneDepthTextureSize_ = glm::ivec2(0);
}

int EditorRenderer::cloudDepthTextureUnit() const {
  GLint maxTextureUnits = 0;
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);

  const int textureUnit = static_cast<int>(blockTextures_.size());
  if (maxTextureUnits <= 0 || textureUnit >= maxTextureUnits) {
    return -1;
  }

  return textureUnit;
}

glm::vec4 EditorRenderer::getFogColor(int depth) const {
  static const glm::vec4 fogColors[] = {
      {0.02f, 0.02f, 0.08f, 1.0f},
      {0.06f, 0.01f, 0.10f, 1.0f},
      {0.10f, 0.02f, 0.02f, 1.0f},
      {0.01f, 0.07f, 0.05f, 1.0f},
      {0.08f, 0.06f, 0.01f, 1.0f},
      {0.02f, 0.05f, 0.10f, 1.0f},
      {0.58431375f, 0.97647059f, 0.89019608f, 1.0f}, // #95f9e3
      {0.11764706f, 0.65882353f, 0.58823529f, 1.0f}, // #1ea896
      {0.47058824f, 0.52156863f, 0.52156863f, 1.0f}, // #788585
  };
  const int paletteSize = static_cast<int>(sizeof(fogColors) / sizeof(fogColors[0]));
  const int index = ((depth % paletteSize) + paletteSize) % paletteSize;
  return fogColors[index];
}

float EditorRenderer::computeFogDensity(int depth, int renderDistance) const {
  const float visibleDistance =
      std::max(28.0f, renderDistance * 16.0f - 10.0f);
  const float depthScale = 1.0f + static_cast<float>(depth) * 0.04f;
  return (4.6f / visibleDistance) * depthScale;
}

} // namespace BiomeMaker
