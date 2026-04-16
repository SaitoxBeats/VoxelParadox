// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_context.hpp
// Purpose: centralizes the runtime services shared by gameplay code during a frame.
// Flow: the runtime loop builds one context per frame and passes it to gameplay entry points.

#pragma once

// 1. Standard Library
#include <string>

// 2. Third-party Libraries

// 3. Local Project Modules
#include "gameplay/gameplay_events.hpp"

class GameAudioController;
class GameChat;
class Player;
class WorldStack;
class hudPortalTracker;

namespace GameplayStatus {
class System;
}

namespace Gameplay {

struct Context {
    Player& player;
    WorldStack& worldStack;
    GameplayStatus::System& gameplayStatus;
    GameAudioController* audioController = nullptr;
    GameChat* gameChat = nullptr;
    hudPortalTracker* portalTracker = nullptr;
    EventQueue* eventQueue = nullptr;
    float dt = 0.0f;

    void emitBlockBroken(
        const glm::ivec3& blockPosition,
        BlockId blockType,
        bool countStats = true,
        const glm::ivec3& blockNormal = glm::ivec3(0)
    ) const {
        if (eventQueue) {
            eventQueue->emitBlockBroken(
                blockPosition,
                blockType,
                countStats,
                blockNormal
            );
        }
    }

    void emitBlockPlaced(const glm::ivec3& blockPosition, BlockId blockType) const {
        if (eventQueue) {
            eventQueue->emitBlockPlaced(blockPosition, blockType);
        }
    }

    void emitItemAcquired(const InventoryItem& item, int itemAmount) const {
        if (eventQueue) {
            eventQueue->emitItemAcquired(item, itemAmount);
        }
    }

    void emitItemCollected(const InventoryItem& item, int itemAmount) const {
        if (eventQueue) {
            eventQueue->emitItemCollected(item, itemAmount);
        }
    }

    void emitPlayerExperienceChanged(
        float experiencePoints,
        int experienceLevel,
        float experienceEarned = 0.0f
    ) const {
        if (eventQueue) {
            eventQueue->emitPlayerExperienceChanged(
                experiencePoints,
                experienceLevel,
                experienceEarned
            );
        }
    }

    void emitPlayerDied() const {
        if (eventQueue) {
            eventQueue->emitPlayerDied();
        }
    }

    void emitPlayerRespawned() const {
        if (eventQueue) {
            eventQueue->emitPlayerRespawned();
        }
    }

    void emitPlayerLevelUp(int previousLevel, int newLevel) const {
        if (eventQueue) {
            eventQueue->emitPlayerLevelUp(previousLevel, newLevel);
        }
    }

    void emitPortalCreated(
        const glm::ivec3& blockPosition,
        const glm::ivec3& blockNormal = glm::ivec3(0)
    ) const {
        if (eventQueue) {
            eventQueue->emitPortalCreated(blockPosition, blockNormal);
        }
    }

    void emitUniverseEntered(
        const glm::ivec3& portalBlock,
        const glm::ivec3& portalNormal,
        const std::string& nextBiomePresetId = {}
    ) const {
        if (eventQueue) {
            eventQueue->emitUniverseEntered(
                portalBlock,
                portalNormal,
                nextBiomePresetId
            );
        }
    }

    void emitUniverseExited(
        const glm::ivec3& portalBlock,
        const glm::ivec3& portalNormal
    ) const {
        if (eventQueue) {
            eventQueue->emitUniverseExited(portalBlock, portalNormal);
        }
    }
};

}  // namespace Gameplay
