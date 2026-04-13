// File: VoxelParadox.Client/src/Systems/gameplay/block_interaction_system.cpp
// Purpose: centralizes player block interaction orchestration behind a reusable gameplay system.
// Flow: Player forwards interaction entry points here, and the system mutates Player state through its facade/friend access.

// 1. Standard Library
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "gameplay/block_interaction_system.hpp"

#include "audio/game_audio_controller.hpp"
#include "gameplay/block_script_runtime.hpp"
#include "gameplay/gameplay_context.hpp"
#include "gameplay/gameplay_script_contexts.hpp"
#include "gameplay/portal_interaction_system.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"
#include "items/item_catalog.hpp"
#include "items/item_use_context.hpp"
#include "player/player.hpp"
#include "player/player_interaction_tuning.hpp"
#include "render/config/hand_animation_config.hpp"
#include "world/block/block.hpp"

namespace {

// --- 1. Interaction Helpers ---

bool rayIntersectsAabb(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const glm::vec3& minBounds,
    const glm::vec3& maxBounds,
    float maxDistance
) {
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

glm::ivec3 targetNormalFromViewDirection(const glm::vec3& direction) {
    const glm::vec3 absoluteDirection = glm::abs(direction);
    glm::ivec3 normal(0);

    if (absoluteDirection.x >= absoluteDirection.y &&
        absoluteDirection.x >= absoluteDirection.z) {
        normal.x = direction.x >= 0.0f ? -1 : 1;
        return normal;
    }

    if (absoluteDirection.y >= absoluteDirection.z) {
        normal.y = direction.y >= 0.0f ? -1 : 1;
        return normal;
    }

    normal.z = direction.z >= 0.0f ? -1 : 1;
    return normal;
}

void runBlockBreakScript(
    const BlockDefinition& definition,
    Player& player,
    Gameplay::Context& gameplayContext,
    FractalWorld* world,
    const glm::ivec3& blockPosition,
    const glm::ivec3& blockNormal,
    BlockId blockType
) {
    if (!definition.script.enabled) {
        return;
    }

    Gameplay::BlockScriptContext scriptContext{};
    scriptContext.inheritFrom(gameplayContext, world);
    scriptContext.setBlockState(blockPosition, blockNormal, blockType);
    scriptContext.player = &player;

    Gameplay::runBlockScriptOnBreak(definition, scriptContext);
}

InventoryItem makeConfiguredBlockDropItem(BlockId blockType) {
    const BlockDefinition& definition = getBlockDefinition(blockType);
    if (definition.data.dropItemId.empty()) {
        return {};
    }

    if (definition.data.dropItemId == "none") {
        return {};
    }

    InventoryItem dropItem{};
    if (tryParseInventoryItem(definition.data.dropItemId, dropItem) &&
        !dropItem.empty()) {
        return dropItem;
    }

    std::printf(
        "[Blocks] Invalid drop_item '%s' for '%s'. No item will drop.\n",
        definition.data.dropItemId.c_str(),
        definition.id.c_str()
    );
    return {};
}

float getBlockDropExperiencePoints(const Player& player, BlockId blockType) {
    if (!hasConfiguredBlockDropItem(blockType)) {
        return 0.0f;
    }

    const float experienceMultiplier = getBlockBreakExperienceMultiplier(blockType);
    if (experienceMultiplier <= 0.0f) {
        return 0.0f;
    }

    return player.getExperiencePerBlock() * experienceMultiplier;
}

}  // namespace

namespace Gameplay {

void BlockInteractionSystem::handleBlockInteraction(
    Player& player,
    Context& gameplayContext
) {
    // --- 1. Gather Frame State ---
    auto& inputActions = InputMapping::InputActionSystem::instance();
    WorldStack& worldStack = gameplayContext.worldStack;
    FractalWorld* world = worldStack.currentWorld();

    const BlockId targetType = (world && player.targeting.hasTarget)
        ? world->getBlock(player.targeting.targetBlock)
        : BlockIds::AIR;

    const bool portalKeyboardTarget = player.targeting.hasTarget &&
        !usesCustomBlockModel(targetType) &&
        targetType == BlockIds::PORTAL;

    const bool sandboxPortalCreationTarget = player.targeting.hasTarget &&
        player.sandboxModeEnabled &&
        !usesCustomBlockModel(targetType) &&
        isSolid(targetType);

    // --- 2. Process Portal/Block Input ---
    if (inputActions.wasPressed(InputActionIds::kPreviewPortal) &&
        (portalKeyboardTarget || sandboxPortalCreationTarget)) {
        if (player.tryPrepareNestedWorld(worldStack, player.targeting.targetBlock)) {
            player.beginNestedPreviewFadeIn(
                player.targeting.targetBlock,
                player.targeting.targetNormal
            );
        }
    }

    if (inputActions.isDown(InputActionIds::kBreakBlock) && player.targeting.hasTarget) {
        updateBlockBreaking(player, gameplayContext);
    }
    else {
        player.targeting.resetBlockBreaking();
    }

    if (inputActions.wasPressed(InputActionIds::kPlaceBlock)) {
        const InventoryItem& selectedItem = player.hotbar.getSelectedItem();

        if (selectedItem.isItem()) {
            const ItemDefinition& selectedItemDefinition = getItemDefinition(selectedItem.itemId);

            if (selectedItemDefinition.behavior) {
                ItemUseContext useContext{};
                useContext.inheritFrom(gameplayContext, world);
                useContext.scriptSession = player.getActiveItemScriptSession();

                if (selectedItemDefinition.behavior->onUse(selectedItemDefinition, useContext)) {
                    player.targeting.resetBlockBreaking();
                    return;
                }
            }
        }

        if (player.targeting.hasTarget) {
            player.targeting.resetBlockBreaking();
            placeBlockAtTarget(player, gameplayContext);
        }
    }

    if (inputActions.wasPressed(InputActionIds::kEnterPortal) &&
        (portalKeyboardTarget || sandboxPortalCreationTarget)) {
        player.targeting.resetBlockBreaking();
        PortalInteractionSystem::beginNestedEntryTransition(
            player,
            worldStack,
            gameplayContext.eventQueue
        );
    }

    if (inputActions.wasPressed(InputActionIds::kAscendDimension)) {
        player.targeting.resetBlockBreaking();

        if (player.sandboxModeEnabled) {
            PortalInteractionSystem::beginAscendTransition(
                player,
                worldStack,
                gameplayContext.eventQueue
            );
            return;
        }

        const InventoryItem& selectedItem = player.hotbar.getSelectedItem();
        const bool hasVersalReady =
            selectedItem.isItem() &&
            player.hotbar.getSelectedCount() > 0 &&
            std::string(getItemId(selectedItem.itemId)) == PlayerExperience::kRewardItemId;

        if (!hasVersalReady) {
            return;
        }

        if (!PortalInteractionSystem::beginAscendTransition(
                player,
                worldStack,
                gameplayContext.eventQueue
            )) {
            return;
        }

        player.hotbar.consumeSelected(1);
    }
}

void BlockInteractionSystem::updateBlockBreaking(
    Player& player,
    Context& gameplayContext
) {
    // --- 1. Validate Block State ---
    WorldStack& worldStack = gameplayContext.worldStack;
    const float dt = gameplayContext.dt;
    FractalWorld* world = worldStack.currentWorld();

    if (!world || !player.targeting.hasTarget) {
        player.targeting.resetBlockBreaking();
        return;
    }

    const BlockId targetType = world->getBlock(player.targeting.targetBlock);
    if (targetType == BlockIds::AIR) {
        player.targeting.resetBlockBreaking();
        return;
    }

    // --- 2. Handle Target Changes ---
    const bool changedTarget =
        !player.targeting.isBreakingBlock ||
        player.targeting.breakingBlock != player.targeting.targetBlock ||
        player.targeting.breakingBlockType != targetType;

    if (changedTarget) {
        player.targeting.isBreakingBlock = true;
        player.targeting.breakingBlock = player.targeting.targetBlock;
        player.targeting.breakingBlockType = targetType;
        player.targeting.breakingTimer = 0.0f;
        player.targeting.breakingProgress = 0.0f;
        player.targeting.breakingHitCooldown =
            PlayerInteractionTuning::kBreakHitRepeatInterval;

        HandAnimation::state.triggerSwing(HandAnimation::config.swingSpeed);

        if (player.audioController) {
            player.audioController->onBlockHit(
                targetType,
                player.targeting.targetBlock,
                true
            );
        }
    }

    // --- 3. Update Timers ---
    const float breakTime = player.getBreakTimeSeconds(targetType);
    if (breakTime <= 0.0f) {
        HandAnimation::state.triggerSwing(HandAnimation::config.swingSpeed);
        breakTargetBlock(player, gameplayContext);
        player.targeting.resetBlockBreaking();
        return;
    }

    player.targeting.breakingTimer =
        glm::min(player.targeting.breakingTimer + dt, breakTime);
    player.targeting.breakingProgress =
        glm::clamp(player.targeting.breakingTimer / breakTime, 0.0f, 1.0f);
    player.targeting.breakingHitCooldown -= dt;

    if (player.targeting.breakingHitCooldown <= 0.0f &&
        player.targeting.breakingTimer < breakTime) {
        HandAnimation::state.triggerSwing(HandAnimation::config.swingSpeed);

        if (player.audioController) {
            player.audioController->onBlockHit(
                targetType,
                player.targeting.targetBlock,
                false
            );
        }
        player.targeting.breakingHitCooldown =
            PlayerInteractionTuning::kBreakHitRepeatInterval;
    }

    // --- 4. Finalize Breaking ---
    if (player.targeting.breakingTimer >= breakTime) {
        breakTargetBlock(player, gameplayContext);
        player.targeting.resetBlockBreaking();
    }
}

void BlockInteractionSystem::breakTargetBlock(
    Player& player,
    Context& gameplayContext
) {
    // --- 1. Validate Target ---
    WorldStack& worldStack = gameplayContext.worldStack;
    FractalWorld* world = worldStack.currentWorld();

    if (!world) {
        return;
    }

    const BlockId brokenType = world->getBlock(player.targeting.targetBlock);
    if (brokenType == BlockIds::AIR) {
        player.targeting.resetBlockBreaking();
        return;
    }

    const InventoryItem harvestTool = player.hotbar.getSelectedItem();
    const BlockDefinition& brokenBlockDefinition = getBlockDefinition(brokenType);

    // --- 2. Portal Destruction Logic ---
    if (brokenType == BlockIds::PORTAL) {
        if (worldStack.deleteUniverseAtPortal(player.targeting.targetBlock)) {
            gameplayContext.emitBlockBroken(player.targeting.targetBlock, brokenType);

            const InventoryItem droppedItem =
                shouldDropBlockItemForTool(harvestTool, brokenType)
                    ? makeConfiguredBlockDropItem(brokenType)
                    : InventoryItem{};
            const bool droppedConfiguredItem = !droppedItem.empty();
            if (droppedConfiguredItem) {
                world->spawnDroppedItem(
                    player.targeting.targetBlock,
                    droppedItem,
                    player.camera.getForward() *
                        PlayerInteractionTuning::kDroppedItemThrowSpeed,
                    getBlockDropExperiencePoints(player, brokenType)
                );
            }

            runBlockBreakScript(
                brokenBlockDefinition,
                player,
                gameplayContext,
                world,
                player.targeting.targetBlock,
                player.targeting.targetNormal,
                brokenType
            );

            player.targeting.clearTargetSelection();
            player.clearNestedPreview();
            return;
        }
    }

    // --- 3. Standard Block Destruction Logic ---
    world->setBlock(player.targeting.targetBlock, BlockIds::AIR);
    gameplayContext.emitBlockBroken(player.targeting.targetBlock, brokenType);

    const InventoryItem brokenBlockDropItem =
        shouldDropBlockItemForTool(harvestTool, brokenType)
            ? makeConfiguredBlockDropItem(brokenType)
            : InventoryItem{};
    const bool droppedBrokenBlockItem = !brokenBlockDropItem.empty();
    if (droppedBrokenBlockItem) {
        world->spawnDroppedItem(
            player.targeting.targetBlock,
            brokenBlockDropItem,
            player.camera.getForward() *
                PlayerInteractionTuning::kDroppedItemThrowSpeed,
            getBlockDropExperiencePoints(player, brokenType)
        );
    }

    runBlockBreakScript(
        brokenBlockDefinition,
        player,
        gameplayContext,
        world,
        player.targeting.targetBlock,
        player.targeting.targetNormal,
        brokenType
    );

    // --- 4. Handle Attached Blocks ---
    const glm::ivec3 supportedBlockPos =
        player.targeting.targetBlock + glm::ivec3(0, 1, 0);
    const BlockId attachedTopBlockType = world->getBlock(supportedBlockPos);
    const BlockDefinition& attachedTopBlockDefinition =
        getBlockDefinition(attachedTopBlockType);

    if (attachedTopBlockDefinition.supportRule.mode == BlockSupportMode::ALLOW_LIST) {
        world->setBlock(supportedBlockPos, BlockIds::AIR);
        gameplayContext.emitBlockBroken(supportedBlockPos, attachedTopBlockType);

        const InventoryItem attachedBlockDropItem =
            shouldDropBlockItemForTool(harvestTool, attachedTopBlockType)
                ? makeConfiguredBlockDropItem(attachedTopBlockType)
                : InventoryItem{};
        const bool droppedAttachedBlockItem = !attachedBlockDropItem.empty();
        if (droppedAttachedBlockItem) {
            world->spawnDroppedItem(
                supportedBlockPos,
                attachedBlockDropItem,
                player.camera.getForward() *
                    (PlayerInteractionTuning::kDroppedItemThrowSpeed * 0.35f),
                getBlockDropExperiencePoints(player, attachedTopBlockType)
            );
        }

        runBlockBreakScript(
            attachedTopBlockDefinition,
            player,
            gameplayContext,
            world,
            supportedBlockPos,
            glm::ivec3(0),
            attachedTopBlockType
        );
    }

    player.targeting.resetBlockBreaking();
}

void BlockInteractionSystem::placeBlockAtTarget(
    Player& player,
    Context& gameplayContext
) {
    // --- 1. Validate Initial State ---
    WorldStack& worldStack = gameplayContext.worldStack;
    FractalWorld* world = worldStack.currentWorld();

    if (!world) {
        return;
    }

    const InventoryItem& selectedItem = player.hotbar.getSelectedItem();
    if (!isPlaceableInventoryItem(selectedItem) ||
        player.hotbar.getSelectedCount() <= 0) {
        return;
    }

    const glm::ivec3 placePos =
        player.targeting.targetBlock + player.targeting.targetNormal;
    const BlockId existingType = world->getBlock(placePos);

    if (!isReplaceableBlock(existingType)) {
        return;
    }

    if (isSolid(selectedItem.blockType) &&
        player.doesBlockOverlapCurrentBody(placePos)) {
        return;
    }

    // --- 2. Special Placement Logic ---
    const BlockDefinition& selectedBlockDefinition =
        getBlockDefinition(selectedItem.blockType);

    if (selectedBlockDefinition.requiresTopPlacement) {
        if (player.targeting.targetNormal != glm::ivec3(0, 1, 0)) {
            return;
        }

        const BlockId supportType =
            world->getBlock(placePos + glm::ivec3(0, -1, 0));
        if (!canSupportTopPlacedBlock(supportType, selectedItem.blockType)) {
            return;
        }
    }

    // --- 3. Existing Block Interactions ---
    const BlockDefinition& existingBlockDefinition = getBlockDefinition(existingType);
    if (existingBlockDefinition.supportRule.mode == BlockSupportMode::ALLOW_LIST &&
        selectedItem.blockType != existingType) {
        const InventoryItem existingBlockDropItem =
            shouldDropBlockItemForTool(selectedItem, existingType)
                ? makeConfiguredBlockDropItem(existingType)
                : InventoryItem{};
        if (!existingBlockDropItem.empty()) {
            world->spawnDroppedItem(
                placePos,
                existingBlockDropItem,
                player.camera.getForward() *
                    (PlayerInteractionTuning::kDroppedItemThrowSpeed * 0.35f)
            );
        }

        gameplayContext.emitBlockBroken(placePos, existingType, false);
    }

    // --- 4. Finalize Placement ---
    const BlockId placedBlockType = selectedItem.blockType;
    world->setBlock(placePos, placedBlockType);
    player.hotbar.consumeSelected(1);
    gameplayContext.emitBlockPlaced(placePos, placedBlockType);
    HandAnimation::state.triggerPunch(HandAnimation::config.punchSpeed,
                                      HandAnimation::config.placeHoldDelay);
}

void BlockInteractionSystem::dropSelectedItem(
    Player& player,
    WorldStack& worldStack
) {
    // --- 1. Validate Item ---
    FractalWorld* world = worldStack.currentWorld();
    if (!world ||
        !player.hotbar.hasSelectedItem() ||
        player.hotbar.getSelectedCount() <= 0) {
        return;
    }

    // --- 2. Setup Drop Parameters ---
    const InventoryItem droppedItem = player.hotbar.getSelectedItem();
    const glm::vec3 throwDirection = glm::normalize(player.camera.getForward());
    const glm::vec3 spawnPosition =
        player.camera.position +
        throwDirection * PlayerInteractionTuning::kDroppedItemSpawnDistance;
    const glm::vec3 initialVelocity =
        throwDirection * PlayerInteractionTuning::kDroppedItemThrowSpeed;

    if (!player.hotbar.consumeSelected(1)) {
        return;
    }

    // --- 3. Spawn Item ---
    world->spawnDroppedItemAtPosition(
        spawnPosition,
        droppedItem,
        initialVelocity,
        PlayerInteractionTuning::kDroppedItemPickupDelay
    );
}

void BlockInteractionSystem::dropHeldItem(
    Player& player,
    WorldStack& worldStack
) {
    FractalWorld* world = worldStack.currentWorld();
    if (!world) {
        return;
    }

    PlayerHotbar::Slot held = player.hotbar.takeHeldSlot();
    if (held.empty()) {
        return;
    }

    const glm::vec3 throwDirection = glm::normalize(player.camera.getForward());
    const glm::vec3 spawnPosition =
        player.camera.position +
        throwDirection * PlayerInteractionTuning::kDroppedItemSpawnDistance;
    const glm::vec3 initialVelocity =
        throwDirection * PlayerInteractionTuning::kDroppedItemThrowSpeed;

    for (int i = 0; i < held.count; ++i) {
        world->spawnDroppedItemAtPosition(
            spawnPosition,
            held.item,
            initialVelocity,
            PlayerInteractionTuning::kDroppedItemPickupDelay
        );
    }
}

void BlockInteractionSystem::spawnEnemyAtTarget(
    Player& player,
    WorldStack& worldStack,
    EnemyType type
) {
    // --- 1. Validate Target ---
    FractalWorld* world = worldStack.currentWorld();
    if (!world ||
        !player.targeting.hasTarget ||
        player.targeting.targetNormal != glm::ivec3(0, 1, 0)) {
        return;
    }

    // --- 2. Calculate Parameters ---
    const glm::vec3 spawnPosition =
        glm::vec3(player.targeting.targetBlock) + glm::vec3(0.5f, 1.0f, 0.5f);
    const glm::vec3 forward = glm::normalize(player.camera.getForward());
    const float yawDegrees = glm::degrees(std::atan2(-forward.x, -forward.z));

    // --- 3. Spawn Enemy ---
    world->spawnEnemy(type, spawnPosition, yawDegrees);
}

void BlockInteractionSystem::doRaycast(
    Player& player,
    FractalWorld* world
) {
    // --- 1. Initialization ---
    player.targeting.clearTargetOnly();

    const glm::vec3 origin = player.camera.position;
    const glm::vec3 dir = player.camera.getForward();

    glm::ivec3 current = glm::ivec3(glm::floor(origin));
    glm::ivec3 step(0);
    glm::vec3 tMax(0.0f);
    glm::vec3 tDelta(0.0f);

    // --- 2. Step Calculation ---
    for (int axis = 0; axis < 3; ++axis) {
        if (dir[axis] > 0.0f) {
            step[axis] = 1;
            tMax[axis] =
                (std::floor(origin[axis]) + 1.0f - origin[axis]) / dir[axis];
            tDelta[axis] = 1.0f / dir[axis];
        }
        else if (dir[axis] < 0.0f) {
            step[axis] = -1;
            tMax[axis] =
                (origin[axis] - std::floor(origin[axis])) / (-dir[axis]);
            tDelta[axis] = 1.0f / (-dir[axis]);
        }
        else {
            step[axis] = 0;
            tMax[axis] = 1e30f;
            tDelta[axis] = 1e30f;
        }
    }

    // --- 3. DDA Traversal ---
    const int maxSteps = static_cast<int>(player.breakRange / 0.5f);
    for (int i = 0; i < maxSteps; ++i) {
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

        if (t > player.breakRange) {
            break;
        }

        const BlockId hitType = world->getBlock(current);
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
        if (!rayIntersectsAabb(origin, dir, blockMin, blockMax, player.breakRange)) {
            continue;
        }

        // --- 4. Hit Result ---
        player.targeting.hasTarget = true;
        player.targeting.targetBlock = current;
        player.targeting.targetNormal = glm::ivec3(0);
        player.targeting.targetNormal[axis] = -step[axis];
        return;
    }

    // --- 4. Overlap Fallback ---
    if (tryTargetOverlappingBodyBlock(player, world)) {
        return;
    }
}

bool BlockInteractionSystem::tryTargetOverlappingBodyBlock(
    Player& player,
    FractalWorld* world
) {
    if (!world) {
        return false;
    }

    const glm::vec3 origin = player.camera.position;
    const glm::vec3 dir = player.camera.getForward();

    auto trySelectOverlappingBlock =
        [&](const glm::ivec3& blockPos, bool overlapsBody) {
            if (!overlapsBody) {
                return false;
            }

            const BlockId blockType = world->getBlock(blockPos);
            if (!canTargetBlock(blockType)) {
                return false;
            }

            glm::vec3 selectionMin(0.0f);
            glm::vec3 selectionMax(1.0f);
            if (!getBlockSelectionBounds(blockType, selectionMin, selectionMax)) {
                return false;
            }

            const glm::vec3 blockMin = glm::vec3(blockPos) + selectionMin;
            const glm::vec3 blockMax = glm::vec3(blockPos) + selectionMax;
            if (!rayIntersectsAabb(origin, dir, blockMin, blockMax, player.breakRange)) {
                return false;
            }

            player.targeting.hasTarget = true;
            player.targeting.targetBlock = blockPos;
            player.targeting.targetNormal = targetNormalFromViewDirection(dir);
            return true;
        };

    const glm::ivec3 headBlock = glm::ivec3(glm::floor(player.camera.position));
    const BlockId headBlockType = world->getBlock(headBlock);
    if (trySelectOverlappingBlock(
            headBlock,
            canTargetBlock(headBlockType) &&
                isSolid(headBlockType) &&
                player.doesBlockOverlapCurrentBody(headBlock)
        )) {
        return true;
    }

    return false;
}

}  // namespace Gameplay
