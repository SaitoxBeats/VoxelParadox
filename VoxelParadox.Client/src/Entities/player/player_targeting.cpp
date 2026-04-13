// File: VoxelParadox.Client/src/Entities/player/player_targeting.cpp
// Purpose: implements the reusable reset helpers for player target and block-breaking state.
// Flow: Player delegates interaction-state cleanup here instead of keeping those fields and helpers inline.

#include "player_targeting.hpp"

void PlayerTargeting::clearTargetSelection() {
    clearTargetOnly();
    resetBlockBreaking();
}

void PlayerTargeting::clearTargetOnly() {
    hasTarget = false;
    targetBlock = glm::ivec3(0);
    targetNormal = glm::ivec3(0);
}

void PlayerTargeting::resetBlockBreaking() {
    isBreakingBlock = false;
    breakingBlock = glm::ivec3(0);
    breakingBlockType = BlockIds::AIR;
    breakingTimer = 0.0f;
    breakingProgress = 0.0f;
    breakingHitCooldown = 0.0f;
}
