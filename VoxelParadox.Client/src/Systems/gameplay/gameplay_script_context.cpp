// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_script_context.cpp
// Purpose: implements the reusable gameplay-facing base context shared by scriptable systems.
// Flow: specialized contexts reuse these helpers to inherit runtime services and trigger common gameplay actions.

// 1. Standard Library

// 2. Third-party Libraries

// 3. Local Project Modules
#include "gameplay/gameplay_script_context.hpp"

#include "player/player.hpp"
#include "world/generation/fractal_world.hpp"

namespace Gameplay {

void ScriptContext::inheritFrom(
    const Context& gameplayContext,
    FractalWorld* currentWorld
) {
    player = &gameplayContext.player;
    worldStack = &gameplayContext.worldStack;
    world = currentWorld;
    audioController = gameplayContext.audioController;
    gameChat = gameplayContext.gameChat;
    portalTracker = gameplayContext.portalTracker;
    eventQueue = gameplayContext.eventQueue;
}

void ScriptContext::clearTargetState() {
    setTargetState(
        false,
        glm::ivec3(0),
        glm::ivec3(0),
        BlockIds::AIR
    );
}

void ScriptContext::setTargetState(
    bool hasTargetValue,
    const glm::ivec3& blockPosition,
    const glm::ivec3& blockNormal,
    BlockId blockTypeValue
) {
    hasTarget = hasTargetValue;
    targetBlock = blockPosition;
    targetNormal = blockNormal;
    targetBlockType = blockTypeValue;
}

void ScriptContext::inheritTargetFromPlayer(FractalWorld* currentWorld) {
    FractalWorld* targetWorld = currentWorld ? currentWorld : world;
    if (!player || !player->hasTargetBlock()) {
        clearTargetState();
        return;
    }

    const glm::ivec3 blockPosition = player->getTargetBlock();
    const BlockId blockType =
        targetWorld ? targetWorld->getBlock(blockPosition) : BlockIds::AIR;

    setTargetState(
        true,
        blockPosition,
        player->getTargetNormal(),
        blockType
    );
}

bool ScriptContext::tryAddInventoryItem(
    const InventoryItem& item,
    int amount
) const {
    return player && player->tryAddItemToInventory(item, amount, eventQueue);
}

bool ScriptContext::tryRemoveInventoryItem(
    const InventoryItem& item,
    int amount
) const {
    return player && player->tryRemoveInventoryItem(item, amount);
}

int ScriptContext::countInventoryItem(const InventoryItem& item) const {
    return player ? player->countInventoryItem(item) : 0;
}

bool ScriptContext::tryConsumeSelectedInventoryItem(int amount) const {
    return player && player->tryConsumeSelectedInventoryItem(amount);
}

void ScriptContext::setPlayerLifePoints(int lifePoints) const {
    if (!player) {
        return;
    }

    player->setLifePoints(lifePoints);
}

void ScriptContext::setPlayerExperience(float value) const {
    if (!player) {
        return;
    }

    player->setExperiencePoints(value, eventQueue);
}

void ScriptContext::addPlayerExperience(float value) const {
    if (!player) {
        return;
    }

    player->addExperiencePoints(value, worldStack, eventQueue);
}

bool ScriptContext::tryCreatePortalForTargetBlock() const {
    return player &&
        worldStack &&
        hasTarget &&
        player->tryCreatePortalForTargetBlock(*worldStack, eventQueue);
}

}  // namespace Gameplay
