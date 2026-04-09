// player_interaction.cpp
// Unity mental model: Player interaction logic.
// Handles raycast target selection, block breaking/placing, item drops, and portal action hotkeys.

#pragma region Includes

// 1. Standard Library
#include <cmath>
#include <algorithm>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "player.hpp"
#include "audio/game_audio_controller.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

#pragma endregion

namespace {

#pragma region 1. Interaction Helpers
    // --- 1. Interaction Helpers ---

    bool rayIntersectsAabb(const glm::vec3& origin, const glm::vec3& direction,
        const glm::vec3& minBounds, const glm::vec3& maxBounds,
        float maxDistance) {
        float tMin = 0.0f;
        float tMax = maxDistance;

        for (int axis = 0; axis < 3; ++axis) {
            const float component = direction[axis];
            if (std::fabs(component) < 1e-6f) {
                if (origin[axis] < minBounds[axis] || origin[axis] > maxBounds[axis]) {
                    return false;
                }
                continue;
            }

            const float invComponent = 1.0f / component;
            float t0 = (minBounds[axis] - origin[axis]) * invComponent;
            float t1 = (maxBounds[axis] - origin[axis]) * invComponent;

            if (t0 > t1) {
                const float temp = t0;
                t0 = t1;
                t1 = temp;
            }

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);

            if (tMin > tMax) {
                return false;
            }
        }

        return tMax >= 0.0f && tMin <= maxDistance;
    }

#pragma endregion

}  // namespace

#pragma region 2. Block Interaction Input
// --- 2. Block Interaction Input ---

void Player::handleBlockInteraction(WorldStack& worldStack, float dt) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    // --- 1. Get Initial State ---
    FractalWorld* world = worldStack.currentWorld();

    const BlockType targetType = (world && hasTarget) ? world->getBlock(targetBlock) : BlockType::AIR;

    const bool portalActionTarget = hasTarget &&
        !usesCustomBlockModel(targetType) &&
        (targetType == BlockType::PORTAL || isSolid(targetType));

    // --- 2. Process Input Actions ---
    if (inputActions.wasPressed(InputActionIds::kPreviewPortal) && portalActionTarget) {
        if (tryPrepareNestedWorld(worldStack, targetBlock)) {
            beginNestedPreviewFadeIn(targetBlock, targetNormal);
        }
    }

    if (inputActions.isDown(InputActionIds::kBreakBlock) && hasTarget) {
        updateBlockBreaking(worldStack, dt);
    }
    else {
        resetBlockBreaking();
    }

    if (inputActions.wasPressed(InputActionIds::kPlaceBlock) && hasTarget) {
        resetBlockBreaking();
        placeBlockAtTarget(worldStack);
    }

    if (inputActions.wasPressed(InputActionIds::kEnterPortal) && portalActionTarget) {
        resetBlockBreaking();
        beginNestedEntryTransition(worldStack);
    }

    if (inputActions.wasPressed(InputActionIds::kAscendDimension)) {
        resetBlockBreaking();
        beginAscendTransition(worldStack);
    }
}

#pragma endregion

#pragma region 3. Block Breaking Logic
// --- 3. Block Breaking Logic ---

void Player::updateBlockBreaking(WorldStack& worldStack, float dt) {
    // --- 1. Validate Block State ---
    FractalWorld* world = worldStack.currentWorld();

    if (!world || !hasTarget) {
        resetBlockBreaking();
        return;
    }

    const BlockType targetType = world->getBlock(targetBlock);

    if (targetType == BlockType::AIR) {
        resetBlockBreaking();
        return;
    }

    // --- 2. Handle Target Changes ---
    const bool changedTarget = !isBreakingBlock || breakingBlock != targetBlock || breakingBlockType != targetType;

    if (changedTarget) {
        isBreakingBlock = true;
        breakingBlock = targetBlock;
        breakingBlockType = targetType;
        breakingTimer = 0.0f;
        breakingProgress = 0.0f;
        breakingHitCooldown = kBreakHitRepeatInterval;

        if (audioController) {
            audioController->onBlockHit(targetType, targetBlock, true);
        }
    }

    // --- 3. Update Timers ---
    const float breakTime = getBreakTimeSeconds(targetType);

    if (breakTime <= 0.0f) {
        breakTargetBlock(worldStack);
        resetBlockBreaking();
        return;
    }

    breakingTimer = glm::min(breakingTimer + dt, breakTime);
    breakingProgress = glm::clamp(breakingTimer / breakTime, 0.0f, 1.0f);
    breakingHitCooldown -= dt;

    if (audioController && breakingHitCooldown <= 0.0f && breakingTimer < breakTime) {
        audioController->onBlockHit(targetType, targetBlock, false);
        breakingHitCooldown = kBreakHitRepeatInterval;
    }

    // --- 4. Finalize Breaking ---
    if (breakingTimer >= breakTime) {
        breakTargetBlock(worldStack);
        resetBlockBreaking();
    }
}

void Player::breakTargetBlock(WorldStack& worldStack) {
    // --- 1. Validate Target ---
    FractalWorld* world = worldStack.currentWorld();

    if (!world) {
        return;
    }

    const BlockType brokenType = world->getBlock(targetBlock);

    if (brokenType == BlockType::AIR) {
        resetBlockBreaking();
        return;
    }

    const InventoryItem harvestTool = hotbar.getSelectedItem();

    // --- 2. Portal Destruction Logic ---
    if (brokenType == BlockType::PORTAL) {
        if (worldStack.deleteUniverseAtPortal(targetBlock)) {

            if (audioController) {
                audioController->onBlockBroken(brokenType, targetBlock);
            }

            if (shouldDropBlockItemForTool(harvestTool, brokenType)) {
                world->spawnDroppedItem(
                    targetBlock,
                    makeInventoryBlock(brokenType),
                    camera.getForward() * kDroppedItemThrowSpeed
                );
            }

            clearTargetSelection();
            clearNestedPreview();
            return;
        }
    }

    // --- 3. Standard Block Destruction Logic ---
    world->setBlock(targetBlock, BlockType::AIR);

    if (audioController) {
        audioController->onBlockBroken(brokenType, targetBlock);
    }

    if (shouldDropBlockItemForTool(harvestTool, brokenType)) {
        world->spawnDroppedItem(
            targetBlock,
            makeInventoryBlock(brokenType),
            camera.getForward() * kDroppedItemThrowSpeed
        );
    }

    // --- 4. Handle Attached Blocks ---
    const glm::ivec3 supportedBlockPos = targetBlock + glm::ivec3(0, 1, 0);

    if (world->getBlock(supportedBlockPos) == BlockType::MEMBRANE_WIRE) {
        world->setBlock(supportedBlockPos, BlockType::AIR);

        if (audioController) {
            audioController->onBlockBroken(BlockType::MEMBRANE_WIRE, supportedBlockPos);
        }

        world->spawnDroppedItem(
            supportedBlockPos,
            makeInventoryBlock(BlockType::MEMBRANE_WIRE),
            camera.getForward() * (kDroppedItemThrowSpeed * 0.35f)
        );
    }

    resetBlockBreaking();
}

#pragma endregion

#pragma region 4. Block Placement
// --- 4. Block Placement ---

void Player::placeBlockAtTarget(WorldStack& worldStack) {
    // --- 1. Validate Initial State ---
    FractalWorld* world = worldStack.currentWorld();

    if (!world) {
        return;
    }

    const InventoryItem& selectedItem = hotbar.getSelectedItem();

    if (!isPlaceableInventoryItem(selectedItem) || hotbar.getSelectedCount() <= 0) {
        return;
    }

    const glm::ivec3 placePos = targetBlock + targetNormal;
    const BlockType existingType = world->getBlock(placePos);

    if (!isReplaceableBlock(existingType)) {
        return;
    }

    // --- 2. Special Placement Logic ---
    if (selectedItem.blockType == BlockType::MEMBRANE_WIRE) {
        if (targetNormal != glm::ivec3(0, 1, 0)) {
            return;
        }

        const BlockType supportType = world->getBlock(placePos + glm::ivec3(0, -1, 0));

        if (!canSupportTopPlacedBlock(supportType, selectedItem.blockType)) {
            return;
        }
    }

    // --- 3. Existing Block Interactions ---
    if (existingType == BlockType::MEMBRANE_WIRE && selectedItem.blockType != BlockType::MEMBRANE_WIRE) {
        world->spawnDroppedItem(
            placePos,
            makeInventoryBlock(existingType),
            camera.getForward() * (kDroppedItemThrowSpeed * 0.35f)
        );

        if (audioController) {
            audioController->onBlockBroken(existingType, placePos);
        }
    }

    // --- 4. Finalize Placement ---
    const BlockType placedBlockType = selectedItem.blockType;

    world->setBlock(placePos, placedBlockType);
    hotbar.consumeSelected(1);

    if (audioController) {
        audioController->onBlockPlaced(placedBlockType, placePos);
    }
}

#pragma endregion

#pragma region 5. World Interaction (Drops & Spawns)
// --- 5. World Interaction (Drops & Spawns) ---

void Player::dropSelectedItem(WorldStack& worldStack) {
    // --- 1. Validate Item ---
    FractalWorld* world = worldStack.currentWorld();

    if (!world || !hotbar.hasSelectedItem() || hotbar.getSelectedCount() <= 0) {
        return;
    }

    // --- 2. Setup Drop Parameters ---
    const InventoryItem droppedItem = hotbar.getSelectedItem();
    const glm::vec3 throwDirection = glm::normalize(camera.getForward());

    const glm::vec3 spawnPosition = camera.position + throwDirection * kDroppedItemSpawnDistance;
    const glm::vec3 initialVelocity = throwDirection * kDroppedItemThrowSpeed;

    if (!hotbar.consumeSelected(1)) {
        return;
    }

    // --- 3. Spawn Item ---
    world->spawnDroppedItemAtPosition(
        spawnPosition,
        droppedItem,
        initialVelocity,
        kDroppedItemPickupDelay
    );
}

void Player::spawnEnemyAtTarget(WorldStack& worldStack, EnemyType type) {
    // --- 1. Validate Target ---
    FractalWorld* world = worldStack.currentWorld();

    if (!world || !hasTarget || targetNormal != glm::ivec3(0, 1, 0)) {
        return;
    }

    // --- 2. Calculate Parameters ---
    const glm::vec3 spawnPosition = glm::vec3(targetBlock) + glm::vec3(0.5f, 1.0f, 0.5f);
    const glm::vec3 forward = glm::normalize(camera.getForward());
    const float yawDegrees = glm::degrees(std::atan2(-forward.x, -forward.z));

    // --- 3. Spawn Enemy ---
    world->spawnEnemy(type, spawnPosition, yawDegrees);
}

#pragma endregion

#pragma region 6. Raycast & Targeting
// --- 6. Raycast & Targeting ---

void Player::doRaycast(FractalWorld* world) {
    // DDA stepping finds the first solid block and the face that was hit.

    // --- 1. Initialization ---
    clearTargetOnly();

    const glm::vec3 origin = camera.position;
    const glm::vec3 dir = camera.getForward();

    glm::ivec3 current = glm::ivec3(glm::floor(origin));
    glm::ivec3 step(0);
    glm::vec3 tMax(0.0f);
    glm::vec3 tDelta(0.0f);

    // --- 2. Step Calculation ---
    for (int axis = 0; axis < 3; axis++) {
        if (dir[axis] > 0.0f) {
            step[axis] = 1;
            tMax[axis] = (std::floor(origin[axis]) + 1.0f - origin[axis]) / dir[axis];
            tDelta[axis] = 1.0f / dir[axis];
        }
        else if (dir[axis] < 0.0f) {
            step[axis] = -1;
            tMax[axis] = (origin[axis] - std::floor(origin[axis])) / (-dir[axis]);
            tDelta[axis] = 1.0f / (-dir[axis]);
        }
        else {
            step[axis] = 0;
            tMax[axis] = 1e30f;
            tDelta[axis] = 1e30f;
        }
    }

    // --- 3. DDA Traversal ---
    const int maxSteps = static_cast<int>(breakRange / 0.5f);

    for (int i = 0; i < maxSteps; i++) {
        int axis = 0;

        if (tMax.x < tMax.y) {
            axis = tMax.x < tMax.z ? 0 : 2;
        }
        else {
            axis = tMax.y < tMax.z ? 1 : 2;
        }

        current[axis] += step[axis];
        const float t = tMax[axis];
        tMax[axis] += tDelta[axis];

        if (t > breakRange) {
            break;
        }

        const BlockType hitType = world->getBlock(current);
        if (!canTargetBlock(hitType)) {
            continue;
        }

        glm::vec3 selectionMin(0.0f);
        glm::vec3 selectionMax(1.0f);
        if (!getBlockSelectionBounds(hitType, selectionMin, selectionMax)) {
            continue;
        }

        const glm::vec3 blockMin = glm::vec3(current) + selectionMin;
        const glm::vec3 blockMax = glm::vec3(current) + selectionMax;
        if (!rayIntersectsAabb(origin, dir, blockMin, blockMax, breakRange)) {
            continue;
        }

        // --- 4. Hit Result ---
        hasTarget = true;
        targetBlock = current;
        targetNormal = glm::ivec3(0);
        targetNormal[axis] = -step[axis];
        return;
    }
}

#pragma endregion