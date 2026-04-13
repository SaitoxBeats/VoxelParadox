// File: VoxelParadox.Client/src/Entities/player/player_targeting.hpp
// Purpose: groups the player's target selection and block-breaking state into one dedicated module.
// Flow: interaction systems mutate this state while renderer and HUD read it through the Player facade.

#pragma once

// 1. Third-party Libraries
#include <glm/glm.hpp>

// 2. Local Project Modules
#include "world/block/block_catalog.hpp"

class PlayerTargeting {
public:
    bool hasTarget = false;
    glm::ivec3 targetBlock{ 0 };
    glm::ivec3 targetNormal{ 0 };
    bool isBreakingBlock = false;
    glm::ivec3 breakingBlock{ 0 };
    BlockId breakingBlockType = BlockIds::AIR;
    float breakingTimer = 0.0f;
    float breakingProgress = 0.0f;
    float breakingHitCooldown = 0.0f;

    void clearTargetSelection();
    void clearTargetOnly();
    void resetBlockBreaking();
};
