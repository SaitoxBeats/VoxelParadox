#pragma once

// 1. Standard Library
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

// 3. Local Project Modules
#include "engine/meshing/mesh_types.hpp"
#include "engine/shader.hpp"
#include "world/cloud/cloud_generator.hpp"

namespace VoxelGame {

struct CloudRenderContext {
  const BiomePreset* preset = nullptr;
  std::uint32_t seed = 0;
  int depth = 0;
  glm::vec3 cameraPosition{0.0f};
  glm::mat4 viewProjection{1.0f};
  glm::vec4 fogColor{0.0f, 0.0f, 0.0f, 1.0f};
  float fogDensity = 0.0f;
  float timeSeconds = 0.0f;
  int fallbackRenderDistance = 5;
  float alphaMultiplier = 1.0f;
};

class CloudRenderer {
public:
  void cleanup();
  void render(const CloudRenderContext& context, Shader& blockShader);

private:
  using Vertex = ENGINE::Meshing::MeshVertex;

  struct CachedPage {
    GLuint vao = 0;
    GLuint vbo = 0;
    std::size_t vboCapacityBytes = 0;
    int vertexCount = 0;
    std::uint64_t lastUsedFrame = 0;
  };

  struct CloudPageKeyHasher {
    std::size_t operator()(const Clouds::CloudPageKey& key) const {
      return static_cast<std::size_t>(Clouds::hashCloudPageKey(key));
    }
  };

  std::unordered_map<Clouds::CloudPageKey, CachedPage, CloudPageKeyHasher>
      pageCache_{};
  std::uint64_t frameCounter_ = 0;

  void releasePage(CachedPage& page);
  void uploadPage(CachedPage& page, const std::vector<Vertex>& vertices);
  void buildPageMesh(const Clouds::CloudPageDescriptor& descriptor,
                     CachedPage& page);
  int computeBuildBudget(
      const std::vector<Clouds::CloudPageDescriptor>& pages) const;
  void pruneCache(std::size_t visiblePageCount);
};

} // namespace VoxelGame
