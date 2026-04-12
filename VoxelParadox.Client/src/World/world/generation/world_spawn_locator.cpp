#include "world/generation/world_spawn_locator.hpp"

#include <array>
#include <limits>

#include "world/block/block.hpp"
#include "world/generation/fractal_world.hpp"

namespace {

bool isPositionFree(FractalWorld& world, const glm::vec3& eyePosition, float radius,
                    float bodyHeight, float eyeHeight) {
    const glm::vec3 feetPosition = eyePosition - glm::vec3(0.0f, eyeHeight, 0.0f);
    const glm::vec3 minCorner = feetPosition + glm::vec3(-radius, 0.0f, -radius);
    const glm::vec3 maxCorner =
        feetPosition + glm::vec3(radius, bodyHeight, radius) - glm::vec3(0.0005f);
    const glm::ivec3 minBlock = glm::ivec3(glm::floor(minCorner));
    const glm::ivec3 maxBlock = glm::ivec3(glm::floor(maxCorner));

    for (int bx = minBlock.x; bx <= maxBlock.x; ++bx) {
        for (int by = minBlock.y; by <= maxBlock.y; ++by) {
            for (int bz = minBlock.z; bz <= maxBlock.z; ++bz) {
                if (isSolid(world.getBlock(glm::ivec3(bx, by, bz)))) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool hasGroundSupport(FractalWorld& world, const glm::vec3& eyePosition, float radius,
                      float eyeHeight, const WorldSpawnLocatorConfig& config) {
    const float sampleRadius = glm::max(0.0f, radius - config.supportInset);
    const int supportY =
        static_cast<int>(std::floor(eyePosition.y - eyeHeight - config.groundProbeDistance));
    const std::array<glm::vec2, 5> samples = {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(-sampleRadius, -sampleRadius),
        glm::vec2(-sampleRadius, sampleRadius),
        glm::vec2(sampleRadius, -sampleRadius),
        glm::vec2(sampleRadius, sampleRadius),
    };

    for (const glm::vec2& offset : samples) {
        const glm::ivec3 block(
            static_cast<int>(std::floor(eyePosition.x + offset.x)),
            supportY,
            static_cast<int>(std::floor(eyePosition.z + offset.y)));
        if (isSolid(world.getBlock(block))) {
            return true;
        }
    }

    return false;
}

int freeNeighborCount(FractalWorld& world, const glm::ivec3& cell) {
    int freeCount = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (!isSolid(world.getBlock(cell + glm::ivec3(dx, dy, dz)))) {
                    ++freeCount;
                }
            }
        }
    }

    return freeCount;
}

} // namespace

glm::vec3 findNearestGroundSpawnPosition(FractalWorld& world,
                                         const glm::vec3& spawnAnchor,
                                         float playerRadius,
                                         float bodyHeight,
                                         float eyeHeight,
                                         const WorldSpawnLocatorConfig& config) {
    world.primeImmediateArea(spawnAnchor, config.primeRadiusChunks);

    const glm::ivec3 base = glm::ivec3(glm::floor(spawnAnchor));
    const glm::vec3 fallback =
        spawnAnchor + glm::vec3(0.0f, eyeHeight, 0.0f);

    glm::vec3 bestPosition = fallback;
    float bestScore = -std::numeric_limits<float>::infinity();
    bool found = false;

    for (int dy = -config.searchRadiusY; dy <= config.searchRadiusY; ++dy) {
        for (int dx = -config.searchRadiusXZ; dx <= config.searchRadiusXZ; ++dx) {
            for (int dz = -config.searchRadiusXZ; dz <= config.searchRadiusXZ; ++dz) {
                const int horizontalDistance2 = dx * dx + dz * dz;
                if (horizontalDistance2 >
                    config.searchRadiusXZ * config.searchRadiusXZ) {
                    continue;
                }

                const glm::ivec3 cell = base + glm::ivec3(dx, dy, dz);
                const glm::vec3 candidate(cell.x + 0.5f,
                                          cell.y + eyeHeight,
                                          cell.z + 0.5f);
                if (!isPositionFree(world, candidate, playerRadius, bodyHeight, eyeHeight) ||
                    !hasGroundSupport(world, candidate, playerRadius, eyeHeight, config)) {
                    continue;
                }

                const float verticalPenalty =
                    static_cast<float>(dy * dy) * 0.35f;
                const float opennessBonus =
                    static_cast<float>(freeNeighborCount(world, cell)) * 10.0f;
                const float score =
                    opennessBonus - static_cast<float>(horizontalDistance2) - verticalPenalty;
                if (!found || score > bestScore) {
                    bestScore = score;
                    bestPosition = candidate;
                    found = true;
                }
            }
        }
    }

    return bestPosition;
}
