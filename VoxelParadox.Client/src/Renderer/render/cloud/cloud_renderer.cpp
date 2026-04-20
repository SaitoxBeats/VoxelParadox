#include "render/cloud/cloud_renderer.hpp"

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstddef>

// 2. Third-party Libraries
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 3. Local Project Modules
#include "engine/meshing/greedy_mesh.hpp"
#include "world/block/block.hpp"

namespace {

float cloudDepthFadeDistance(VoxelGame::Clouds::CloudQuality quality) {
  if (quality == VoxelGame::Clouds::CloudQuality::LOW) {
    return 2.0f;
  }

  if (quality == VoxelGame::Clouds::CloudQuality::HIGH) {
    return 4.0f;
  }

  return 3.0f;
}

} // namespace

namespace VoxelGame {

void CloudRenderer::cleanup() {
  for (auto& [key, page] : pageCache_) {
    (void)key;
    releasePage(page);
  }
  pageCache_.clear();
  frameCounter_ = 0;
}

void CloudRenderer::render(const CloudRenderContext& context,
                           Shader& blockShader) {
  frameCounter_++;

  if (!context.preset) {
    pruneCache(0);
    return;
  }

  std::vector<Clouds::CloudPageDescriptor> pages =
      Clouds::collectVisibleCloudPages(*context.preset, context.seed,
                                       context.depth, context.cameraPosition,
                                       context.timeSeconds,
                                       context.fallbackRenderDistance,
                                       context.quality);
  if (pages.empty()) {
    pruneCache(0);
    return;
  }

  int buildBudget = computeBuildBudget(pages, context.quality);
  struct DrawPage {
    Clouds::CloudPageDescriptor descriptor{};
    CachedPage* page = nullptr;
  };
  std::vector<DrawPage> drawPages;
  drawPages.reserve(pages.size());

  for (const Clouds::CloudPageDescriptor& descriptor : pages) {
    if (!isPageVisible(descriptor, context.viewProjection)) {
      continue;
    }

    auto found = pageCache_.find(descriptor.key);
    if (found == pageCache_.end()) {
      if (buildBudget <= 0) {
        continue;
      }

      CachedPage page{};
      buildPageMesh(descriptor, page);
      found = pageCache_.emplace(descriptor.key, page).first;
      buildBudget--;
    }

    CachedPage& cachedPage = found->second;
    cachedPage.lastUsedFrame = frameCounter_;
    if (cachedPage.vertexCount <= 0) {
      continue;
    }

    drawPages.push_back(DrawPage{descriptor, &cachedPage});
  }

  if (drawPages.empty()) {
    pruneCache(pages.size());
    return;
  }

  std::sort(drawPages.begin(), drawPages.end(),
            [](const DrawPage& left, const DrawPage& right) {
              return left.descriptor.sortDistance2 >
                     right.descriptor.sortDistance2;
            });

  const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
  const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
  const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
  GLboolean depthMask = GL_TRUE;
  GLint blendSrcRgb = GL_ONE;
  GLint blendDstRgb = GL_ZERO;
  GLint blendSrcAlpha = GL_ONE;
  GLint blendDstAlpha = GL_ZERO;
  GLint cullFaceMode = GL_BACK;
  GLint activeTexture = GL_TEXTURE0;
  GLint sceneDepthTextureBinding = 0;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
  glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);

  const bool useSoftParticles =
      context.sceneDepthTexture != 0 && context.sceneDepthTextureUnit >= 0 &&
      context.viewportSize.x > 0 && context.viewportSize.y > 0;

  if (useSoftParticles) {
    glActiveTexture(GL_TEXTURE0 +
                    static_cast<GLenum>(context.sceneDepthTextureUnit));
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &sceneDepthTextureBinding);
    glBindTexture(GL_TEXTURE_2D, context.sceneDepthTexture);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  blockShader.use();
  blockShader.setMat4("uVP", context.viewProjection);
  blockShader.setVec3("uCameraPos", context.cameraPosition);
  blockShader.setVec4("uFogColor", context.fogColor);
  blockShader.setFloat("uFogDensity", context.fogDensity);
  blockShader.setFloat("uTime", context.timeSeconds);
  blockShader.setFloat("uAoStrength", 0.25f);
  blockShader.setVec4("uBiomeTint", glm::vec4(1.0f));
  blockShader.setInt("uUseLocalMaterialSpace", 0);
  blockShader.setInt("uCloudPass", 1);
  blockShader.setInt("uCloudQuality", static_cast<int>(context.quality));
  blockShader.setInt("uCloudSceneDepth",
                     std::max(context.sceneDepthTextureUnit, 0));
  blockShader.setInt("uCloudUseSoftParticles", useSoftParticles ? 1 : 0);
  blockShader.setVec2("uCloudViewportSize", glm::vec2(context.viewportSize));
  blockShader.setMat4("uCloudInvProjection", context.inverseProjection);
  blockShader.setFloat("uCloudDepthFadeDistance",
                       cloudDepthFadeDistance(context.quality));
  blockShader.setFloat("uCloudSoftness",
                       context.quality == Clouds::CloudQuality::LOW
                           ? 0.34f
                           : (context.quality == Clouds::CloudQuality::HIGH
                                  ? 0.58f
                                  : 0.46f));
  blockShader.setInt("uPointLightCount", 0);
  blockShader.setVec3("uBreakBlockCenter", glm::vec3(0.0f));
  blockShader.setFloat("uBreakProgress", 0.0f);
  blockShader.setVec3("uHighlightBlockCenter", glm::vec3(0.0f));
  blockShader.setFloat("uHighlightActive", 0.0f);

  for (const DrawPage& drawPage : drawPages) {
    if (!drawPage.page || drawPage.page->vao == 0 ||
        drawPage.page->vertexCount <= 0) {
      continue;
    }

    const glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(drawPage.descriptor.movementOffset.x, 0.0f,
                  drawPage.descriptor.movementOffset.y));
    blockShader.setMat4("uModel", model);
    blockShader.setFloat("uAlpha",
                         drawPage.descriptor.opacity *
                             std::clamp(context.alphaMultiplier, 0.0f, 1.0f));

    glBindVertexArray(drawPage.page->vao);
    glDrawArrays(GL_TRIANGLES, 0, drawPage.page->vertexCount);
  }

  glBindVertexArray(0);
  blockShader.setMat4("uModel", glm::mat4(1.0f));
  blockShader.setFloat("uAlpha", 1.0f);
  blockShader.setFloat("uAoStrength", 1.0f);
  blockShader.setInt("uCloudPass", 0);
  blockShader.setInt("uCloudQuality",
                     static_cast<int>(Clouds::CloudQuality::MEDIUM));
  blockShader.setInt("uCloudUseSoftParticles", 0);
  blockShader.setFloat("uCloudDepthFadeDistance", 0.0f);
  blockShader.setVec2("uCloudViewportSize", glm::vec2(0.0f));
  blockShader.setMat4("uCloudInvProjection", glm::mat4(1.0f));
  blockShader.setFloat("uCloudSoftness", 0.0f);

  if (useSoftParticles) {
    glActiveTexture(GL_TEXTURE0 +
                    static_cast<GLenum>(context.sceneDepthTextureUnit));
    glBindTexture(GL_TEXTURE_2D,
                  static_cast<GLuint>(sceneDepthTextureBinding));
  }
  glActiveTexture(static_cast<GLenum>(activeTexture));

  if (blendEnabled) {
    glEnable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
  }
  glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);

  if (cullEnabled) {
    glEnable(GL_CULL_FACE);
    glCullFace(cullFaceMode);
  } else {
    glDisable(GL_CULL_FACE);
  }

  if (depthEnabled) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  glDepthMask(depthMask);

  pruneCache(pages.size());
}

void CloudRenderer::releasePage(CachedPage& page) {
  if (page.vbo != 0) {
    glDeleteBuffers(1, &page.vbo);
    page.vbo = 0;
  }
  if (page.vao != 0) {
    glDeleteVertexArrays(1, &page.vao);
    page.vao = 0;
  }
  page.vboCapacityBytes = 0;
  page.vertexCount = 0;
}

void CloudRenderer::uploadPage(CachedPage& page,
                               const std::vector<Vertex>& vertices) {
  if (page.vao == 0) {
    glGenVertexArrays(1, &page.vao);
    glGenBuffers(1, &page.vbo);
    glBindVertexArray(page.vao);
    glBindBuffer(GL_ARRAY_BUFFER, page.vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, ao)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, material)));
    glEnableVertexAttribArray(4);
  } else {
    glBindVertexArray(page.vao);
    glBindBuffer(GL_ARRAY_BUFFER, page.vbo);
  }

  const std::size_t requiredBytes = vertices.size() * sizeof(Vertex);
  if (requiredBytes > page.vboCapacityBytes) {
    page.vboCapacityBytes =
        std::max(requiredBytes, page.vboCapacityBytes == 0
                                    ? std::size_t(4096 * sizeof(Vertex))
                                    : page.vboCapacityBytes * 2);
    glBufferData(GL_ARRAY_BUFFER, page.vboCapacityBytes, nullptr,
                 GL_DYNAMIC_DRAW);
  }

  if (requiredBytes > 0) {
    glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, vertices.data());
  }

  page.vertexCount = static_cast<int>(vertices.size());
  glBindVertexArray(0);
}

void CloudRenderer::buildPageMesh(
    const Clouds::CloudPageDescriptor& descriptor,
    CachedPage& page) {
  ENGINE::Meshing::GreedyChunkInput input{};
  input.chunkCoord = glm::ivec3(descriptor.key.pageX,
                                descriptor.key.layerSegment,
                                descriptor.key.pageZ);
  input.chunkOrigin = glm::vec3(descriptor.origin);
  input.chunkSize = Clouds::kCloudPageSize;
  input.depth = descriptor.key.depth;
  input.enableMerging = true;
  input.sampleBlock = [descriptor](const glm::ivec3& localPos) -> BlockId {
    return Clouds::sampleCloudCell(descriptor, localPos)
               ? static_cast<BlockId>(BlockIds::CLOUD_CHUNK)
               : static_cast<BlockId>(BlockIds::AIR);
  };
  input.isRenderableBlock = [](BlockId block) {
    return block == static_cast<BlockId>(BlockIds::CLOUD_CHUNK);
  };
  input.isOccludingBlock = [](BlockId block) {
    return block == static_cast<BlockId>(BlockIds::CLOUD_CHUNK);
  };
  input.resolveFaceMaterial = [descriptor](BlockId block, int faceDirection) {
    ENGINE::Meshing::FaceMaterialDesc material{};
    material.color = getBlockColor(block, descriptor.key.depth, faceDirection);
    material.material = getBlockMaterialId(block);
    material.transparent = true;
    return material;
  };

  ENGINE::Meshing::GreedyMeshResult meshResult =
      ENGINE::Meshing::buildGreedyChunkMesh(input);
  uploadPage(page, meshResult.vertices);
}

bool CloudRenderer::isPageVisible(
    const Clouds::CloudPageDescriptor& descriptor,
    const glm::mat4& viewProjection) const {
  const glm::vec3 extents(
      static_cast<float>(Clouds::kCloudPageSize) * 0.5f + 2.0f,
      static_cast<float>(std::max(descriptor.segmentHeight, 1)) * 0.5f + 2.0f,
      static_cast<float>(Clouds::kCloudPageSize) * 0.5f + 2.0f);

  const glm::vec4 rows[4] = {
      glm::row(viewProjection, 0),
      glm::row(viewProjection, 1),
      glm::row(viewProjection, 2),
      glm::row(viewProjection, 3),
  };
  const glm::vec4 planes[6] = {
      rows[3] + rows[0],
      rows[3] - rows[0],
      rows[3] + rows[1],
      rows[3] - rows[1],
      rows[3] + rows[2],
      rows[3] - rows[2],
  };

  for (const glm::vec4& plane : planes) {
    const glm::vec3 normal(plane.x, plane.y, plane.z);
    const float radius = extents.x * std::abs(normal.x) +
                         extents.y * std::abs(normal.y) +
                         extents.z * std::abs(normal.z);
    if (glm::dot(normal, descriptor.worldCenter) + plane.w + radius < 0.0f) {
      return false;
    }
  }

  return true;
}

int CloudRenderer::computeBuildBudget(
    const std::vector<Clouds::CloudPageDescriptor>& pages,
    Clouds::CloudQuality quality) const {
  std::vector<int> seenModules;
  int budget = 0;

  for (const Clouds::CloudPageDescriptor& page : pages) {
    if (!page.module) {
      continue;
    }
    if (std::find(seenModules.begin(), seenModules.end(),
                  page.key.moduleIndex) != seenModules.end()) {
      continue;
    }

    seenModules.push_back(page.key.moduleIndex);
    budget += std::clamp(page.module->cloudGenerator.maxMeshPagesPerFrame,
                         1, 64);
  }

  const int qualityCap =
      quality == Clouds::CloudQuality::LOW
          ? 1
          : (quality == Clouds::CloudQuality::HIGH ? 8 : 3);
  return std::clamp(budget, 1, qualityCap);
}

void CloudRenderer::pruneCache(std::size_t visiblePageCount) {
  const std::size_t softLimit = std::max<std::size_t>(visiblePageCount + 128, 512);
  for (auto iterator = pageCache_.begin(); iterator != pageCache_.end();) {
    const bool stale =
        frameCounter_ > iterator->second.lastUsedFrame + 180;
    const bool overLimit = pageCache_.size() > softLimit &&
                           iterator->second.lastUsedFrame != frameCounter_;
    if (!stale && !overLimit) {
      ++iterator;
      continue;
    }

    releasePage(iterator->second);
    iterator = pageCache_.erase(iterator);
  }
}

} // namespace VoxelGame
