#include "world/cloud/cloud_generator.hpp"

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstring>

namespace VoxelGame::Clouds {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct CloudProfile {
  float scale = 1.0f;
  float xStretch = 1.0f;
  float zStretch = 1.0f;
  float threshold = 0.56f;
  float coverageWeight = 0.58f;
  float verticalWeight = 0.34f;
  float detailWeight = 0.24f;
  float streakWeight = 0.0f;
  float verticalPower = 1.6f;
  int octaves = 4;
};

struct CloudQualityTuning {
  int maxRenderDistance = 4;
  int maxVisiblePages = 256;
  int maxLayerScan = 12;
  float lod1DistanceBlocks = 48.0f;
  float lod2DistanceBlocks = 84.0f;
};

int floorDiv(int value, int divisor) {
  if (divisor <= 0) {
    return 0;
  }
  return value >= 0 ? (value / divisor) : ((value - (divisor - 1)) / divisor);
}

std::uint32_t hash3i(int x, int y, int z, std::uint32_t seed) {
  std::uint32_t hash = seed;
  hash ^= static_cast<std::uint32_t>(x) * 0x9e3779b1u;
  hash ^= static_cast<std::uint32_t>(y) * 0x85ebca77u;
  hash ^= static_cast<std::uint32_t>(z) * 0xc2b2ae3du;
  hash ^= hash >> 16;
  hash *= 0x7feb352du;
  hash ^= hash >> 15;
  hash *= 0x846ca68bu;
  hash ^= hash >> 16;
  return hash;
}

float hash01(std::uint32_t value) {
  return static_cast<float>(value & 0x00FFFFFFu) / 16777216.0f;
}

float fade(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float valueNoise3(float x, float y, float z, std::uint32_t seed) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int z0 = static_cast<int>(std::floor(z));
  const float tx = fade(x - static_cast<float>(x0));
  const float ty = fade(y - static_cast<float>(y0));
  const float tz = fade(z - static_cast<float>(z0));

  const auto corner = [seed](int x, int y, int z) {
    return hash01(hash3i(x, y, z, seed));
  };

  const float c000 = corner(x0, y0, z0);
  const float c100 = corner(x0 + 1, y0, z0);
  const float c010 = corner(x0, y0 + 1, z0);
  const float c110 = corner(x0 + 1, y0 + 1, z0);
  const float c001 = corner(x0, y0, z0 + 1);
  const float c101 = corner(x0 + 1, y0, z0 + 1);
  const float c011 = corner(x0, y0 + 1, z0 + 1);
  const float c111 = corner(x0 + 1, y0 + 1, z0 + 1);

  const float x00 = lerp(c000, c100, tx);
  const float x10 = lerp(c010, c110, tx);
  const float x01 = lerp(c001, c101, tx);
  const float x11 = lerp(c011, c111, tx);
  const float y0v = lerp(x00, x10, ty);
  const float y1v = lerp(x01, x11, ty);
  return lerp(y0v, y1v, tz);
}

float fbm3(glm::vec3 p, int octaves, std::uint32_t seed) {
  float sum = 0.0f;
  float amplitude = 0.5f;
  float normalization = 0.0f;

  for (int octave = 0; octave < octaves; ++octave) {
    sum += valueNoise3(p.x, p.y, p.z, seed + static_cast<std::uint32_t>(octave) * 977u) *
           amplitude;
    normalization += amplitude;
    p *= 2.03f;
    amplitude *= 0.52f;
  }

  return normalization > 0.0f ? sum / normalization : 0.0f;
}

CloudQualityTuning tuningFor(CloudQuality quality) {
  switch (quality) {
  case CloudQuality::LOW:
    return {2, 96, 8, 28.0f, 48.0f};
  case CloudQuality::HIGH:
    return {8, 768, 24, 80.0f, 144.0f};
  case CloudQuality::MEDIUM:
  default:
    return {4, 256, 14, 48.0f, 84.0f};
  }
}

CloudProfile profileFor(CloudType type) {
  switch (type) {
  case CloudType::CIRRUS:
    return {1.35f, 0.22f, 2.80f, 0.72f, 0.35f, 0.18f, 0.18f, 0.28f, 2.8f, 3};
  case CloudType::CIRROCUMULUS:
    return {1.10f, 1.10f, 1.10f, 0.66f, 0.46f, 0.24f, 0.32f, 0.08f, 2.3f, 4};
  case CloudType::CIRROSTRATUS:
    return {1.65f, 0.85f, 1.70f, 0.58f, 0.42f, 0.30f, 0.14f, 0.18f, 3.0f, 3};
  case CloudType::ALTOCUMULUS:
    return {0.95f, 1.25f, 1.00f, 0.60f, 0.54f, 0.30f, 0.34f, 0.04f, 1.8f, 4};
  case CloudType::ALTOSTRATUS:
    return {1.40f, 1.00f, 1.45f, 0.52f, 0.58f, 0.34f, 0.18f, 0.10f, 2.6f, 4};
  case CloudType::NIMBOSTRATUS:
    return {1.15f, 1.20f, 1.20f, 0.46f, 0.70f, 0.42f, 0.26f, 0.03f, 1.7f, 5};
  case CloudType::STRATUS:
    return {1.75f, 1.45f, 1.45f, 0.48f, 0.64f, 0.46f, 0.12f, 0.02f, 3.4f, 3};
  case CloudType::STRATOCUMULUS:
    return {1.05f, 1.35f, 1.10f, 0.54f, 0.62f, 0.36f, 0.30f, 0.05f, 2.1f, 4};
  case CloudType::CUMULUS:
  case CloudType::RANDOM:
  default:
    return {0.82f, 1.00f, 1.00f, 0.56f, 0.58f, 0.42f, 0.34f, 0.02f, 1.45f, 5};
  }
}

std::uint32_t mixHash(std::uint32_t hash, std::uint32_t value) {
  hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  return hash;
}

std::uint32_t hashFloat(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

CloudType resolveCloudType(const CloudGeneratorModule& module,
                           std::uint32_t seed,
                           int moduleIndex,
                           int layerIndex,
                           int pageX,
                           int pageZ) {
  if (module.cloudType != CloudType::RANDOM) {
    return module.cloudType;
  }

  const std::uint32_t hash =
      hash3i(pageX + moduleIndex * 17, layerIndex, pageZ, seed ^ 0xC10DCAFEu);
  return static_cast<CloudType>(static_cast<int>(hash % 9u));
}

int layerBaseYFor(const CloudGeneratorModule& module,
                  std::uint32_t seed,
                  int moduleIndex,
                  int layerIndex) {
  int jitter = 0;
  if (module.verticalJitter > 0) {
    const std::uint32_t hash =
        hash3i(moduleIndex, layerIndex, 0, seed ^ 0xB16B00B5u);
    jitter = static_cast<int>(std::round(
        (hash01(hash) * 2.0f - 1.0f) *
        static_cast<float>(module.verticalJitter)));
  }
  return module.baseY + layerIndex * module.verticalSpacing + jitter;
}

bool intersectsRange(int aMin, int aMax, int bMin, int bMax) {
  return !(aMax < bMin || aMin > bMax);
}

void appendModulePages(const BiomePreset& preset,
                       const BiomeModule& module,
                       int moduleIndex,
                       std::uint32_t seed,
                       int depth,
                       const glm::vec3& cameraPosition,
                       float timeSeconds,
                       int fallbackRenderDistance,
                       CloudQuality quality,
                       std::vector<CloudPageDescriptor>& outPages) {
  (void)preset;
  const CloudGeneratorModule& settings = module.cloudGenerator;
  const CloudQualityTuning tuning = tuningFor(quality);
  const int worldRenderDistance =
      fallbackRenderDistance > 0 ? fallbackRenderDistance : settings.renderDistance;
  const int renderDistance = std::clamp(
      std::min({settings.renderDistance, worldRenderDistance,
                tuning.maxRenderDistance}),
      1, 16);
  const int spacing = std::max(settings.verticalSpacing, settings.layerHeight + 1);
  const int layerHeight = std::max(settings.layerHeight, 1);
  const glm::vec2 movement = movementOffsetFor(settings, timeSeconds);
  const glm::vec3 cloudCamera(
      cameraPosition.x - movement.x,
      cameraPosition.y,
      cameraPosition.z - movement.y);

  const int centerPageX =
      floorDiv(static_cast<int>(std::floor(cloudCamera.x)), kCloudPageSize);
  const int centerPageZ =
      floorDiv(static_cast<int>(std::floor(cloudCamera.z)), kCloudPageSize);
  const int verticalWindow =
      std::max(renderDistance * kCloudPageSize, spacing * 2);
  int minWorldY = static_cast<int>(std::floor(cameraPosition.y)) - verticalWindow;
  int maxWorldY = static_cast<int>(std::floor(cameraPosition.y)) + verticalWindow;

  if (!settings.infiniteY) {
    minWorldY = std::max(minWorldY, settings.minY);
    maxWorldY = std::min(maxWorldY, settings.maxY);
    if (maxWorldY < minWorldY) {
      return;
    }
  }

  int minLayer =
      floorDiv(minWorldY - settings.baseY - settings.verticalJitter - layerHeight,
               spacing) -
      1;
  int maxLayer =
      floorDiv(maxWorldY - settings.baseY + settings.verticalJitter, spacing) + 1;
  const int maxLayerScan =
      std::max(4, std::min(tuning.maxLayerScan, renderDistance * 2 + 6));
  if (maxLayer - minLayer + 1 > maxLayerScan) {
    const int centerLayer =
        floorDiv(static_cast<int>(std::floor(cameraPosition.y)) - settings.baseY,
                 spacing);
    minLayer = centerLayer - maxLayerScan / 2;
    maxLayer = minLayer + maxLayerScan - 1;
  }
  const std::uint32_t moduleRevision = hashCloudModule(module);
  const int segmentCount =
      std::max(1, (layerHeight + kCloudPageSize - 1) / kCloudPageSize);

  std::vector<CloudPageDescriptor> modulePages;
  modulePages.reserve(static_cast<std::size_t>((renderDistance * 2 + 1) *
                                               (renderDistance * 2 + 1) *
                                               std::max(1, maxLayer - minLayer + 1)));

  for (int layerIndex = minLayer; layerIndex <= maxLayer; ++layerIndex) {
    const int layerBaseY =
        layerBaseYFor(settings, seed, moduleIndex, layerIndex);
    const int layerMaxY = layerBaseY + layerHeight - 1;
    if (!intersectsRange(layerBaseY, layerMaxY, minWorldY, maxWorldY)) {
      continue;
    }
    if (!settings.infiniteY &&
        !intersectsRange(layerBaseY, layerMaxY, settings.minY, settings.maxY)) {
      continue;
    }

    for (int segment = 0; segment < segmentCount; ++segment) {
      const int segmentBaseY = layerBaseY + segment * kCloudPageSize;
      const int segmentHeight =
          std::min(kCloudPageSize, layerHeight - segment * kCloudPageSize);
      if (segmentHeight <= 0) {
        continue;
      }

      for (int pageX = centerPageX - renderDistance;
           pageX <= centerPageX + renderDistance; ++pageX) {
        for (int pageZ = centerPageZ - renderDistance;
             pageZ <= centerPageZ + renderDistance; ++pageZ) {
          const CloudType resolvedType =
              resolveCloudType(settings, seed, moduleIndex, layerIndex, pageX, pageZ);
          const glm::ivec3 origin(
              pageX * kCloudPageSize,
              segmentBaseY,
              pageZ * kCloudPageSize);
          const glm::vec3 worldCenter(
              static_cast<float>(origin.x) + movement.x +
                  static_cast<float>(kCloudPageSize) * 0.5f,
              static_cast<float>(origin.y) +
                  static_cast<float>(segmentHeight) * 0.5f,
              static_cast<float>(origin.z) + movement.y +
                  static_cast<float>(kCloudPageSize) * 0.5f);
          const glm::vec3 delta = worldCenter - cameraPosition;
          const float distance = std::sqrt(glm::dot(delta, delta));
          int lod = 0;
          if (distance > tuning.lod2DistanceBlocks) {
            lod = 2;
          } else if (distance > tuning.lod1DistanceBlocks) {
            lod = 1;
          }

          CloudPageDescriptor page{};
          page.key.seed = seed;
          page.key.revision = moduleRevision;
          page.key.depth = depth;
          page.key.moduleIndex = moduleIndex;
          page.key.pageX = pageX;
          page.key.pageZ = pageZ;
          page.key.layerIndex = layerIndex;
          page.key.layerSegment = segment;
          page.key.resolvedType = static_cast<int>(resolvedType);
          page.key.lod = lod;
          page.module = &module;
          page.resolvedType = resolvedType;
          page.origin = origin;
          page.movementOffset = movement;
          page.worldCenter = worldCenter;
          page.layerBaseY = layerBaseY;
          page.layerHeight = layerHeight;
          page.segmentHeight = segmentHeight;
          page.lod = lod;
          page.opacity = settings.opacity;
          page.sortDistance2 = glm::dot(delta, delta);
          modulePages.push_back(page);
        }
      }
    }
  }

  std::sort(modulePages.begin(), modulePages.end(),
            [](const CloudPageDescriptor& left,
               const CloudPageDescriptor& right) {
              return left.sortDistance2 < right.sortDistance2;
            });

  const std::size_t visibleLimit =
      static_cast<std::size_t>(std::max(
          16, std::min(settings.maxVisiblePages, tuning.maxVisiblePages)));
  if (modulePages.size() > visibleLimit) {
    modulePages.resize(visibleLimit);
  }

  outPages.insert(outPages.end(), modulePages.begin(), modulePages.end());
}

} // namespace

std::uint32_t hashString(const std::string& value) {
  std::uint32_t hash = 2166136261u;
  for (unsigned char ch : value) {
    hash ^= static_cast<std::uint32_t>(ch);
    hash *= 16777619u;
  }
  return hash;
}

std::uint32_t hashCloudModule(const BiomeModule& module) {
  const CloudGeneratorModule& cloud = module.cloudGenerator;
  std::uint32_t hash = hashString(module.id);
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.cloudType));
  hash = mixHash(hash, hashFloat(cloud.speed));
  hash = mixHash(hash, hashFloat(cloud.directionDegrees));
  hash = mixHash(hash, hashFloat(cloud.coverage));
  hash = mixHash(hash, hashFloat(cloud.opacity));
  hash = mixHash(hash, hashFloat(cloud.densityThreshold));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.cellSize));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.jitter));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.layerHeight));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.baseY));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.minY));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.maxY));
  hash = mixHash(hash, cloud.infiniteY ? 1u : 0u);
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.verticalSpacing));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.verticalJitter));
  hash = mixHash(hash, static_cast<std::uint32_t>(cloud.renderDistance));
  return hash;
}

std::uint32_t hashCloudPageKey(const CloudPageKey& key) {
  std::uint32_t hash = key.seed ^ key.revision;
  hash = mixHash(hash, static_cast<std::uint32_t>(key.depth));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.moduleIndex));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.pageX));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.pageZ));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.layerIndex));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.layerSegment));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.resolvedType));
  hash = mixHash(hash, static_cast<std::uint32_t>(key.lod));
  return hash;
}

glm::vec2 movementOffsetFor(const CloudGeneratorModule& module,
                            float timeSeconds) {
  const float radians = module.directionDegrees * kPi / 180.0f;
  return glm::vec2(std::cos(radians), std::sin(radians)) *
         (module.speed * std::max(timeSeconds, 0.0f));
}

std::vector<CloudPageDescriptor> collectVisibleCloudPages(
    const BiomePreset& preset,
    std::uint32_t seed,
    int depth,
    const glm::vec3& cameraPosition,
    float timeSeconds,
    int fallbackRenderDistance,
    CloudQuality quality) {
  std::vector<CloudPageDescriptor> pages;

  for (std::size_t index = 0; index < preset.modules.size(); ++index) {
    const BiomeModule& module = preset.modules[index];
    if (!module.enabled || module.type != ModuleType::CLOUD_GENERATOR) {
      continue;
    }

    const std::uint32_t cloudSeed =
        seed ^ hash3i(depth, static_cast<int>(index), 0, 0xC70D51E5u);
    appendModulePages(preset, module, static_cast<int>(index), cloudSeed, depth,
                      cameraPosition, timeSeconds, fallbackRenderDistance,
                      quality, pages);
  }

  std::sort(pages.begin(), pages.end(),
            [](const CloudPageDescriptor& left,
               const CloudPageDescriptor& right) {
              return left.sortDistance2 < right.sortDistance2;
            });
  return pages;
}

bool sampleCloudCell(const CloudPageDescriptor& page,
                     const glm::ivec3& localPos) {
  if (!page.module || page.module->type != ModuleType::CLOUD_GENERATOR) {
    return false;
  }

  const CloudGeneratorModule& settings = page.module->cloudGenerator;
  const glm::ivec3 worldCell = page.origin + localPos;
  if (worldCell.y < page.layerBaseY ||
      worldCell.y >= page.layerBaseY + page.layerHeight) {
    return false;
  }
  if (!settings.infiniteY &&
      (worldCell.y < settings.minY || worldCell.y > settings.maxY)) {
    return false;
  }

  const CloudProfile profile = profileFor(page.resolvedType);
  const float cellSize = static_cast<float>(std::max(settings.cellSize, 1));
  const int lodStride = page.lod <= 0 ? 1 : (page.lod == 1 ? 2 : 4);
  const glm::ivec3 sampledCell =
      page.origin + (localPos / lodStride) * lodStride;
  glm::vec3 samplePoint = glm::vec3(sampledCell) + glm::vec3(0.5f);

  if (settings.jitter > 0) {
    const int jitterCellSize = std::max(settings.cellSize * 4, 4);
    const int jitterX = floorDiv(worldCell.x, jitterCellSize);
    const int jitterZ = floorDiv(worldCell.z, jitterCellSize);
    const std::uint32_t jitterHash =
        hash3i(jitterX, page.key.layerIndex, jitterZ,
               page.key.seed ^ page.key.revision ^ 0x51F15EEDu);
    samplePoint.x += (hash01(jitterHash) * 2.0f - 1.0f) *
                     static_cast<float>(settings.jitter);
    samplePoint.z += (hash01(jitterHash ^ 0xA53A9B4Du) * 2.0f - 1.0f) *
                     static_cast<float>(settings.jitter);
  }

  const float layerT = std::clamp(
      (samplePoint.y - static_cast<float>(page.layerBaseY)) /
          static_cast<float>(std::max(page.layerHeight, 1)),
      0.0f, 1.0f);
  const float vertical =
      1.0f - std::pow(std::abs(layerT * 2.0f - 1.0f), profile.verticalPower);
  const float scale = 1.0f / std::max(cellSize * 10.0f * profile.scale, 1.0f);
  const std::uint32_t baseSeed =
      page.key.seed ^ page.key.revision ^
      static_cast<std::uint32_t>(page.key.moduleIndex * 2654435761u);

  const glm::vec3 basePoint(
      samplePoint.x * scale * profile.xStretch,
      samplePoint.y * scale * 0.35f,
      samplePoint.z * scale * profile.zStretch);
  const int octaveCount = std::max(2, profile.octaves - page.lod);
  const float baseNoise = fbm3(basePoint, octaveCount, baseSeed);
  const float detailNoise =
      page.lod >= 2
          ? 0.5f
          : fbm3(basePoint * 3.35f + glm::vec3(13.7f, 2.9f, -8.4f),
                 page.lod == 1 ? 2 : 3, baseSeed ^ 0x7F4A7C15u);
  const float streak =
      0.5f + 0.5f *
                 std::sin(samplePoint.x * scale * 28.0f +
                          samplePoint.z * scale * 4.0f +
                          baseNoise * 5.0f);
  const float coverage =
      std::clamp(settings.coverage, 0.0f, 1.0f) * profile.coverageWeight;
  const float threshold =
      std::clamp(settings.densityThreshold, 0.0f, 1.5f);
  const float density =
      baseNoise * 0.62f + detailNoise * profile.detailWeight +
      vertical * profile.verticalWeight + streak * profile.streakWeight +
      coverage - threshold;

  return density > 0.0f;
}

} // namespace VoxelGame::Clouds
