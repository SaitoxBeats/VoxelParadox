// enemy_spawn_system.cpp
// Unity mental model: a focused encounter director that decides when and where
// enemies can appear around the player.
// Role: owns the enemy spawn rules, spawn validation, and debug spawn hooks.
// Flow: configuration is sampled once, candidate positions are validated against
// the world and the player, and successful spawns reset the cooldown.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// 2. Third-party Libraries
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// 3. Internal Engine/Core Modules (Foundation)

// 4. Local Project Modules (Specific to this system)
#include "enemy_definition.hpp"
#include "enemy_spawn_system.hpp"
#include "player/player.hpp"
#include "world/generation/fractal_world.hpp"

#pragma endregion

namespace {

#pragma region 1. Configuration & Helpers
// --- 1. Configuration & Helpers ---

struct EnemySpawnConfig {
    EnemyType type = EnemyType::Guy;
    int maxActiveEnemies = 1;
    int spawnAttemptsPerUpdate = 24;
    int minSpawnDistanceBlocks = 12;
    int maxSpawnDistanceBlocks = 34;
    int minEnemySpacingBlocks = 6;
    int minVerticalClearanceBlocks = 4;
    int verticalSearchUpBlocks = 10;
    int verticalSearchDownBlocks = 32;
    float retryDelaySeconds = 15.0f;
    float minSpawnIntervalSeconds = 10.0f * 60.0f;
    float maxSpawnIntervalSeconds = 60.0f * 60.0f;
};

bool gImmediateEnemySpawnDebugRequested = false;

const EnemySpawnConfig& enemySpawnConfig() {
    static const EnemySpawnConfig config{};
    return config;
}

float randomZeroToOne() {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

float randomRange(float minValue, float maxValue) {
    if (maxValue <= minValue) {
        return minValue;
    }

    return minValue + (maxValue - minValue) * randomZeroToOne();
}

glm::vec2 randomSpawnOffset(const EnemySpawnConfig& config) {
    const float angleRadians = randomRange(0.0f, glm::two_pi<float>());
    const float radius =
        randomRange(static_cast<float>(config.minSpawnDistanceBlocks),
                    static_cast<float>(config.maxSpawnDistanceBlocks));

    return glm::vec2(std::cos(angleRadians), std::sin(angleRadians)) * radius;
}

bool hasRequiredVerticalClearance(FractalWorld& world,
                                  const glm::ivec3& supportBlock,
                                  int requiredAirBlocks) {
    for (int offsetY = 1; offsetY <= requiredAirBlocks; offsetY++) {
        if (isSolid(world.getBlock(supportBlock + glm::ivec3(0, offsetY, 0)))) {
            return false;
        }
    }

    return true;
}

bool findSupportBlockNearHeight(FractalWorld& world, const glm::ivec2& columnXZ,
                                int playerY, int verticalSearchUp,
                                int verticalSearchDown, int requiredAirBlocks,
                                glm::ivec3& outSupportBlock) {
    const int maxY = playerY + verticalSearchUp;
    const int minY = playerY - verticalSearchDown;

    for (int y = maxY; y >= minY; y--) {
        const glm::ivec3 supportBlock(columnXZ.x, y, columnXZ.y);
        if (!isSolid(world.getBlock(supportBlock))) {
            continue;
        }

        if (!hasRequiredVerticalClearance(world, supportBlock, requiredAirBlocks)) {
            continue;
        }

        outSupportBlock = supportBlock;
        return true;
    }

    return false;
}

bool aabbIntersectsSolidBlocks(FractalWorld& world, const glm::vec3& minCorner,
                               const glm::vec3& maxCorner) {
    const glm::ivec3 minBlock = glm::ivec3(glm::floor(minCorner));
    const glm::ivec3 maxBlock = glm::ivec3(glm::floor(maxCorner));

    for (int bx = minBlock.x; bx <= maxBlock.x; bx++) {
        for (int by = minBlock.y; by <= maxBlock.y; by++) {
            for (int bz = minBlock.z; bz <= maxBlock.z; bz++) {
                const glm::ivec3 block(bx, by, bz);
                if (!isSolid(world.getBlock(block))) {
                    continue;
                }

                const glm::vec3 blockMin(static_cast<float>(bx),
                                         static_cast<float>(by),
                                         static_cast<float>(bz));
                const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);
                const bool overlaps =
                    maxCorner.x > blockMin.x && minCorner.x < blockMax.x &&
                    maxCorner.y > blockMin.y && minCorner.y < blockMax.y &&
                    maxCorner.z > blockMin.z && minCorner.z < blockMax.z;
                if (overlaps) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool enemyFitsAtSpawnPosition(FractalWorld& world, const EnemyDefinition& definition,
                              const glm::vec3& spawnPosition) {
    const glm::vec3 minCorner =
        spawnPosition + definition.collider.centerOffset - definition.collider.halfExtents;
    const glm::vec3 maxCorner =
        spawnPosition + definition.collider.centerOffset + definition.collider.halfExtents;
    return !aabbIntersectsSolidBlocks(world, minCorner, maxCorner);
}

bool isFarEnoughFromExistingEnemies(const FractalWorld& world,
                                    const glm::vec3& spawnPosition,
                                    float minDistance) {
    const float minDistanceSquared = minDistance * minDistance;

    for (const WorldEnemy& enemy : world.enemies) {
        const glm::vec3 delta = enemy.position - spawnPosition;
        if (glm::dot(delta, delta) < minDistanceSquared) {
            return false;
        }
    }

    return true;
}

float yawTowardPlayerDegrees(const glm::vec3& spawnPosition,
                             const glm::vec3& playerPosition) {
    const glm::vec3 toPlayer = playerPosition - spawnPosition;
    if (glm::dot(toPlayer, toPlayer) <= 0.0001f) {
        return 0.0f;
    }

    const glm::vec3 direction = glm::normalize(toPlayer);
    return glm::degrees(std::atan2(-direction.x, -direction.z));
}

bool tryFindEnemySpawn(FractalWorld& world, const Player& player,
                       const EnemyDefinition& definition,
                       const EnemySpawnConfig& config,
                       glm::vec3& outSpawnPosition, float& outYawDegrees) {
    const glm::vec3 playerPosition = player.camera.position;
    const int playerY = static_cast<int>(std::floor(playerPosition.y));

    for (int attempt = 0; attempt < config.spawnAttemptsPerUpdate; attempt++) {
        const glm::vec2 offset = randomSpawnOffset(config);
        const glm::ivec2 columnXZ(
            static_cast<int>(std::floor(playerPosition.x + offset.x)),
            static_cast<int>(std::floor(playerPosition.z + offset.y)));

        glm::ivec3 supportBlock(0);
        if (!findSupportBlockNearHeight(
                world, columnXZ, playerY, config.verticalSearchUpBlocks,
                config.verticalSearchDownBlocks, config.minVerticalClearanceBlocks,
                supportBlock)) {
            continue;
        }

        const glm::vec3 spawnPosition =
            glm::vec3(supportBlock) + glm::vec3(0.5f, 1.0f, 0.5f);
        const glm::vec3 toPlayer = playerPosition - spawnPosition;
        const float distanceSquared = glm::dot(toPlayer, toPlayer);
        const float minDistanceSquared =
            static_cast<float>(config.minSpawnDistanceBlocks *
                               config.minSpawnDistanceBlocks);
        const float maxDistanceSquared =
            static_cast<float>(config.maxSpawnDistanceBlocks *
                               config.maxSpawnDistanceBlocks);
        if (distanceSquared < minDistanceSquared ||
            distanceSquared > maxDistanceSquared) {
            continue;
        }

        if (!enemyFitsAtSpawnPosition(world, definition, spawnPosition)) {
            continue;
        }

        if (!isFarEnoughFromExistingEnemies(
                world, spawnPosition,
                static_cast<float>(config.minEnemySpacingBlocks))) {
            continue;
        }

        outSpawnPosition = spawnPosition;
        outYawDegrees = yawTowardPlayerDegrees(spawnPosition, playerPosition);
        return true;
    }

    return false;
}

float nextSpawnIntervalSeconds(const EnemySpawnConfig& config) {
    return randomRange(config.minSpawnIntervalSeconds,
                       config.maxSpawnIntervalSeconds);
}

void ensureSpawnCooldownInitialized(FractalWorld& world,
                                    const EnemySpawnConfig& config) {
    if (world.enemySpawnState.cooldownSeconds >= 0.0f) {
        return;
    }

    world.enemySpawnState.cooldownSeconds = nextSpawnIntervalSeconds(config);
}

void setFailureReason(std::string* outFailureReason,
                      const std::string& failureReason) {
    if (outFailureReason) {
        *outFailureReason = failureReason;
    }
}

bool tryGetEnemyDefinitionForSpawn(EnemyType type,
                                   const EnemyDefinition*& outDefinition,
                                   std::string* outFailureReason) {
    std::string definitionError;
    outDefinition = getEnemyDefinition(type, &definitionError);
    if (outDefinition) {
        return true;
    }

    setFailureReason(outFailureReason, definitionError);
    return false;
}

bool canSpawnAnotherEnemy(const FractalWorld& world, const EnemySpawnConfig& config,
                          std::string* outFailureReason) {
    if (static_cast<int>(world.enemies.size()) < config.maxActiveEnemies) {
        return true;
    }

    setFailureReason(outFailureReason,
                     "an enemy is already active in the world.");
    return false;
}

void logEnemySpawn(const FractalWorld& world, EnemyType type,
                   const glm::vec3& spawnPosition, float yawDegrees,
                   float nextCooldownSeconds, const char* spawnReason) {
    const float nextCooldownMinutes = nextCooldownSeconds / 60.0f;
    std::printf(
        "[Enemy Spawn] Spawned enemy type=%d | reason=%s | pos=(%.2f, %.2f, %.2f) | "
        "yaw=%.1f | worldSeed=%u | nextCooldown=%.2f min\n",
        static_cast<int>(type), spawnReason, spawnPosition.x, spawnPosition.y,
        spawnPosition.z, yawDegrees, world.seed, nextCooldownMinutes);
}

#pragma endregion

} // namespace

#pragma region 2. Public API
// --- 2. Public API ---

bool tryForceSpawnWorldEnemyNearPlayer(FractalWorld& world, const Player& player,
                                       EnemyType type,
                                       std::string* outFailureReason) {
    const EnemySpawnConfig& config = enemySpawnConfig();
    if (!canSpawnAnotherEnemy(world, config, outFailureReason)) {
        return false;
    }

    const EnemyDefinition* definition = nullptr;
    if (!tryGetEnemyDefinitionForSpawn(type, definition, outFailureReason)) {
        return false;
    }

    glm::vec3 spawnPosition(0.0f);
    float yawDegrees = 0.0f;
    if (!tryFindEnemySpawn(world, player, *definition, config, spawnPosition,
                           yawDegrees)) {
        setFailureReason(outFailureReason,
                         "no valid location found near the player.");
        return false;
    }

    world.spawnEnemy(type, spawnPosition, yawDegrees);
    world.enemySpawnState.cooldownSeconds = nextSpawnIntervalSeconds(config);
    logEnemySpawn(world, type, spawnPosition, yawDegrees,
                  world.enemySpawnState.cooldownSeconds, "command:normal");
    return true;
}

bool tryForceSpawnWorldEnemyAt(FractalWorld& world, EnemyType type,
                               const glm::vec3& spawnPosition, float yawDegrees,
                               std::string* outFailureReason) {
    const EnemySpawnConfig& config = enemySpawnConfig();
    if (!canSpawnAnotherEnemy(world, config, outFailureReason)) {
        return false;
    }

    const EnemyDefinition* definition = nullptr;
    if (!tryGetEnemyDefinitionForSpawn(type, definition, outFailureReason)) {
        return false;
    }

    const glm::ivec3 supportBlock(
        static_cast<int>(std::floor(spawnPosition.x)),
        static_cast<int>(std::floor(spawnPosition.y - 1.0f)),
        static_cast<int>(std::floor(spawnPosition.z)));
    if (!isSolid(world.getBlock(supportBlock))) {
        setFailureReason(outFailureReason,
                         "the target position is not on top of a solid block.");
        return false;
    }

    if (!hasRequiredVerticalClearance(world, supportBlock,
                                      config.minVerticalClearanceBlocks)) {
        setFailureReason(outFailureReason,
                         "the target position does not have enough vertical clearance.");
        return false;
    }

    if (!enemyFitsAtSpawnPosition(world, *definition, spawnPosition)) {
        setFailureReason(outFailureReason,
                         "the enemy does not fit at the target position.");
        return false;
    }

    world.spawnEnemy(type, spawnPosition, yawDegrees);
    world.enemySpawnState.cooldownSeconds = nextSpawnIntervalSeconds(config);
    logEnemySpawn(world, type, spawnPosition, yawDegrees,
                  world.enemySpawnState.cooldownSeconds, "command:lookat");
    return true;
}

void requestImmediateEnemySpawnDebug() {
    gImmediateEnemySpawnDebugRequested = true;
}

float getRemainingNaturalEnemySpawnCooldownSeconds(const FractalWorld& world) {
    return world.enemySpawnState.cooldownSeconds;
}

void updateWorldEnemySpawning(FractalWorld& world, const Player& player, float dt) {
    const EnemySpawnConfig& config = enemySpawnConfig();
    if (dt <= 0.0f || !player.isAlive()) {
        return;
    }

    ensureSpawnCooldownInitialized(world, config);

    const bool immediateDebugSpawn = gImmediateEnemySpawnDebugRequested;
    gImmediateEnemySpawnDebugRequested = false;

    world.enemySpawnState.cooldownSeconds =
        std::max(0.0f, world.enemySpawnState.cooldownSeconds - dt);

    if (static_cast<int>(world.enemies.size()) >= config.maxActiveEnemies) {
        if (immediateDebugSpawn) {
            std::printf(
                "[Enemy Spawn] Debug spawn blocked: an enemy is already active in the world.\n");
        }

        return;
    }

    if (!immediateDebugSpawn && world.enemySpawnState.cooldownSeconds > 0.0f) {
        return;
    }

    const EnemyDefinition* definition = nullptr;
    std::string failureReason;
    if (!tryGetEnemyDefinitionForSpawn(config.type, definition, &failureReason)) {
        std::printf("[Enemy Spawn] %s\n", failureReason.c_str());
        world.enemySpawnState.cooldownSeconds = config.minSpawnIntervalSeconds;
        return;
    }

    glm::vec3 spawnPosition(0.0f);
    float yawDegrees = 0.0f;
    if (!tryFindEnemySpawn(world, player, *definition, config, spawnPosition,
                           yawDegrees)) {
        if (immediateDebugSpawn) {
            std::printf(
                "[Enemy Spawn] Debug spawn failed: no valid location found near the player.\n");
        }

        world.enemySpawnState.cooldownSeconds = config.retryDelaySeconds;
        return;
    }

    world.spawnEnemy(config.type, spawnPosition, yawDegrees);
    world.enemySpawnState.cooldownSeconds = nextSpawnIntervalSeconds(config);
    logEnemySpawn(world, config.type, spawnPosition, yawDegrees,
                  world.enemySpawnState.cooldownSeconds,
                  immediateDebugSpawn ? "debug" : "scheduled");
}

#pragma endregion
