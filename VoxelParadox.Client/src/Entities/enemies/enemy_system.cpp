// enemy_system.cpp
// Unity mental model: Enemy update loop/coordinator.
// Handles enemy despawning, module updates (look, damage, charge), and spatial checks.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstdio> // Note: Added implicitly via original usage of std::printf

// 2. Third-party Libraries
#include <glm/gtc/matrix_transform.hpp>

// 3. Local Project Modules
#include "enemy_system.hpp"
#include "audio/game_audio_controller.hpp"
#include "enemy_definition.hpp"
#include "enemy_runtime_helpers.hpp"
#include "enemy_spawn_system.hpp"
#include "gameplay/gameplay_status.hpp"
#include "player/player.hpp"
#include "world/fractal_world.hpp"

#pragma endregion

namespace {

#pragma region 1. Math & Spatial Helpers
    // --- 1. Math & Spatial Helpers ---

    glm::vec3 rotateAroundY(const glm::vec3& value, float yawDegrees) {
        // Use the exact same yaw convention as the renderer so look-at math stays relative
        // to the enemy orientation no matter where the enemy is spawned.
        const glm::mat4 rotation = glm::rotate(
            glm::mat4(1.0f),
            glm::radians(yawDegrees),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        return glm::vec3(rotation * glm::vec4(value, 0.0f));
    }

    glm::vec3 tryGetLookOriginModelSpace(const EnemyDefinition& definition,
        const EnemyLookAtPlayerCameraModule& module,
        bool& outFound) {
        const EntityModelPartAsset* part = EnemyRuntime::findModelPart(definition.modelAsset, module.partName);
        outFound = part != nullptr;

        if (!part) {
            return glm::vec3(0.0f);
        }

        return glm::vec3(part->nodeTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    glm::vec3 clampToAabb(const glm::vec3& value, const glm::vec3& minCorner, const glm::vec3& maxCorner) {
        return glm::clamp(value, minCorner, maxCorner);
    }

    bool sphereIntersectsEnemyBox(const Player& player, const WorldEnemy& enemy) {
        const glm::vec3 colliderCenter = enemy.position + rotateAroundY(enemy.collider.centerOffset, enemy.yawDegrees);
        const glm::vec3 minCorner = colliderCenter - enemy.collider.halfExtents;
        const glm::vec3 maxCorner = colliderCenter + enemy.collider.halfExtents;

        const glm::vec3 closestPoint = clampToAabb(player.camera.position, minCorner, maxCorner);
        const glm::vec3 delta = player.camera.position - closestPoint;

        return glm::dot(delta, delta) <= player.playerRadius * player.playerRadius;
    }

    bool rayIntersectsLocalBox(const glm::vec3& origin, const glm::vec3& direction,
        const glm::vec3& halfExtents, float maxDistance) {
        const glm::vec3 minCorner = -halfExtents;
        const glm::vec3 maxCorner = halfExtents;

        float tMin = 0.0f;
        float tMax = maxDistance;

        for (int axis = 0; axis < 3; ++axis) {
            const float axisDirection = direction[axis];

            if (std::abs(axisDirection) < 0.00001f) {
                if (origin[axis] < minCorner[axis] || origin[axis] > maxCorner[axis]) {
                    return false;
                }
                continue;
            }

            const float invDirection = 1.0f / axisDirection;
            float t1 = (minCorner[axis] - origin[axis]) * invDirection;
            float t2 = (maxCorner[axis] - origin[axis]) * invDirection;

            if (t1 > t2) {
                std::swap(t1, t2);
            }

            tMin = glm::max(tMin, t1);
            tMax = glm::min(tMax, t2);

            if (tMin > tMax) {
                return false;
            }
        }

        return tMax >= 0.0f && tMin <= maxDistance;
    }

    bool shouldDespawnEnemyBecauseChunkUnloaded(FractalWorld& world, const WorldEnemy& enemy) {
        const glm::ivec3 enemyChunk = FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(enemy.position)));
        Chunk* loadedChunk = world.getChunkIfLoaded(enemyChunk);

        return !loadedChunk || !loadedChunk->generated;
    }

#pragma endregion

#pragma region 2. Module Update Logic
    // --- 2. Module Update Logic ---

    void updateDamagePlayerOnTouchModule(EnemyDamagePlayerOnTouchModule& module,
        EnemyType type, bool touchingPlayer, Player& player) {
        if (touchingPlayer && !module.touchingPlayerLastFrame) {
            GameplayStatus::System::instance().recordEnemyAttack(type);
            player.applyDamage(module.damagePoints);
        }
        module.touchingPlayerLastFrame = touchingPlayer;
    }

    void updateLookAtPlayerCameraModule(EnemyLookAtPlayerCameraModule& module,
        const WorldEnemy& enemy,
        const EnemyDefinition& definition,
        const Player& player) {
        const float baseModelYawDegrees = enemy.yawDegrees + definition.modelAsset.yawOffsetDegrees;
        bool lookOriginFound = false;

        const glm::vec3 lookOriginModelSpace = tryGetLookOriginModelSpace(definition, module, lookOriginFound);
        const glm::vec3 lookOriginWorldSpace = lookOriginFound
            ? enemy.position + rotateAroundY(lookOriginModelSpace, baseModelYawDegrees)
            : enemy.position;

        const glm::vec3 toCameraLocal = rotateAroundY(player.camera.position - lookOriginWorldSpace, -baseModelYawDegrees);
        const float horizontalLength = std::sqrt(toCameraLocal.x * toCameraLocal.x + toCameraLocal.z * toCameraLocal.z);

        module.currentYawDegrees = glm::degrees(std::atan2(toCameraLocal.x, toCameraLocal.z));
        module.currentPitchDegrees = -glm::clamp(
            glm::degrees(std::atan2(toCameraLocal.y, glm::max(horizontalLength, 0.0001f))),
            -module.maxPitchDegrees,
            module.maxPitchDegrees
        );
    }

    bool updateChargePlayerOnLookTriggerModule(EnemyChargePlayerOnLookTriggerModule& module,
        const EnemyDefinition& definition,
        EnemyType type, WorldEnemy& enemy, Player& player,
        GameAudioController& audioController, float dt) {
        const glm::vec3 scaledTriggerHalfExtents = module.triggerHalfExtents * glm::max(module.triggerSizeMultiplier, 0.01f);

        // --- Activation Check ---
        if (!module.activated) {
            glm::mat4 triggerTransform(1.0f);

            if (EnemyRuntime::tryBuildLookTriggerWorldTransform(enemy, definition, module, triggerTransform)) {
                const glm::mat4 inverseTrigger = glm::inverse(triggerTransform);

                const glm::vec3 localRayOrigin = glm::vec3(inverseTrigger * glm::vec4(player.camera.position, 1.0f));
                const glm::vec3 localRayDirection = glm::normalize(
                    glm::vec3(inverseTrigger * glm::vec4(glm::normalize(player.camera.getForward()), 0.0f))
                );

                if (rayIntersectsLocalBox(localRayOrigin, localRayDirection,
                    scaledTriggerHalfExtents,
                    module.activationMaxDistance)) {
                    module.activated = true;
                    audioController.onEnemyTriggerActivated(enemy.position);
                }
            }
        }

        if (!module.activated) {
            return false;
        }

        // --- Movement Logic ---
        const glm::vec3 targetPosition = player.camera.position + glm::vec3(0.0f, module.targetYOffset, 0.0f);
        const glm::vec3 toPlayer = targetPosition - enemy.position;
        const float distanceToPlayer = glm::length(toPlayer);

        if (distanceToPlayer > 0.0001f) {
            const glm::vec3 direction = toPlayer / distanceToPlayer;
            const float moveDistance = glm::min(distanceToPlayer, module.chargeSpeed * dt);

            enemy.position += direction * moveDistance;
            enemy.yawDegrees = glm::degrees(std::atan2(-direction.x, -direction.z));
        }

        // --- Damage Logic ---
        if (sphereIntersectsEnemyBox(player, enemy)) {
            GameplayStatus::System::instance().recordEnemyAttack(type);
            player.applyDamage(module.damagePoints);
            enemy.pendingDestroy = true;
        }

        return true;
    }

#pragma endregion

}  // namespace

#pragma region 3. Main System Update
// --- 3. Main System Update ---

void updateWorldEnemies(FractalWorld& world, Player& player,
    GameAudioController& audioController, float dt) {
    if (!player.isAlive()) {
        return;
    }

    updateWorldEnemySpawning(world, player, dt);

    if (world.enemies.empty()) {
        return;
    }

    for (WorldEnemy& enemy : world.enemies) {
        // --- 1. Despawn Check ---
        if (shouldDespawnEnemyBecauseChunkUnloaded(world, enemy)) {
            std::printf(
                "[Enemy Spawn] Despawned enemy type=%d because its chunk unloaded.\n",
                static_cast<int>(enemy.type)
            );
            enemy.pendingDestroy = true;
            continue;
        }

        std::string definitionError;
        const EnemyDefinition* definition = getEnemyDefinition(enemy.type, &definitionError);

        if (!definition) {
            continue;
        }

        bool chargeModuleActive = false;

        // --- 2. Update Look Modules ---
        for (EnemyModule& module : enemy.modules) {
            switch (module.type) {
            case EnemyModuleType::LookAtPlayerCamera:
                updateLookAtPlayerCameraModule(module.lookAtPlayerCamera, enemy, *definition, player);
                break;
            case EnemyModuleType::DamagePlayerOnTouch:
            case EnemyModuleType::ChargePlayerOnLookTrigger:
                break;
            }
        }

        // --- 3. Update Charge Modules ---
        for (EnemyModule& module : enemy.modules) {
                if (module.type != EnemyModuleType::ChargePlayerOnLookTrigger) {
                    continue;
                }

                chargeModuleActive = updateChargePlayerOnLookTriggerModule(
                    module.chargePlayerOnLookTrigger, *definition, enemy.type, enemy,
                    player, audioController, dt
                ) || chargeModuleActive;
        }

        if (enemy.pendingDestroy) {
            continue;
        }

        // --- 4. Update Damage Modules ---
        if (!chargeModuleActive) {
            const bool touchingPlayer = sphereIntersectsEnemyBox(player, enemy);

            for (EnemyModule& module : enemy.modules) {
                if (module.type != EnemyModuleType::DamagePlayerOnTouch) {
                    continue;
                }
                updateDamagePlayerOnTouchModule(
                    module.damagePlayerOnTouch, enemy.type, touchingPlayer, player
                );
            }
        }
    }

    // --- 5. Cleanup Destroyed Enemies ---
    world.enemies.erase(
        std::remove_if(
            world.enemies.begin(), world.enemies.end(),
            [](const WorldEnemy& enemy) { return enemy.pendingDestroy; }
        ),
        world.enemies.end()
    );
}

#pragma endregion
