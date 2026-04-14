#pragma once

// 1. Standard Library
#include <cstdint>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "world/biome/biome_preset.hpp"

namespace VoxelGame::Clouds {

inline constexpr int kCloudPageSize = 16;

struct CloudPageKey {
  std::uint32_t seed = 0;
  std::uint32_t revision = 0;
  int depth = 0;
  int moduleIndex = 0;
  int pageX = 0;
  int pageZ = 0;
  int layerIndex = 0;
  int layerSegment = 0;
  int resolvedType = 0;

  bool operator==(const CloudPageKey& other) const {
    return seed == other.seed && revision == other.revision &&
           depth == other.depth && moduleIndex == other.moduleIndex &&
           pageX == other.pageX && pageZ == other.pageZ &&
           layerIndex == other.layerIndex &&
           layerSegment == other.layerSegment &&
           resolvedType == other.resolvedType;
  }
};

struct CloudPageDescriptor {
  CloudPageKey key{};
  const BiomeModule* module = nullptr;
  CloudType resolvedType = CloudType::CUMULUS;
  glm::ivec3 origin{0};
  glm::vec2 movementOffset{0.0f};
  glm::vec3 worldCenter{0.0f};
  int layerBaseY = 0;
  int layerHeight = 1;
  float opacity = 0.38f;
  float sortDistance2 = 0.0f;
};

std::uint32_t hashString(const std::string& value);
std::uint32_t hashCloudModule(const BiomeModule& module);
std::uint32_t hashCloudPageKey(const CloudPageKey& key);

glm::vec2 movementOffsetFor(const CloudGeneratorModule& module,
                            float timeSeconds);

std::vector<CloudPageDescriptor> collectVisibleCloudPages(
    const BiomePreset& preset,
    std::uint32_t seed,
    int depth,
    const glm::vec3& cameraPosition,
    float timeSeconds,
    int fallbackRenderDistance);

bool sampleCloudCell(const CloudPageDescriptor& page,
                     const glm::ivec3& localPos);

} // namespace VoxelGame::Clouds
