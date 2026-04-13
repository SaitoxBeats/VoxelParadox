// File: VoxelParadox.Client/src/Entities/player/player_interaction.cpp
// Purpose: keeps Player-facing interaction entry points thin while delegating heavy logic to gameplay systems.
// Flow: external callers still talk to Player, but the implementation now routes through BlockInteractionSystem.

// 1. Local Project Modules
#include "player.hpp"

#include "gameplay/block_interaction_system.hpp"

void Player::handleBlockInteraction(
    Gameplay::Context& gameplayContext
) {
    Gameplay::BlockInteractionSystem::handleBlockInteraction(*this, gameplayContext);
}

void Player::updateBlockBreaking(
    Gameplay::Context& gameplayContext
) {
    Gameplay::BlockInteractionSystem::updateBlockBreaking(*this, gameplayContext);
}

void Player::breakTargetBlock(
    Gameplay::Context& gameplayContext
) {
    Gameplay::BlockInteractionSystem::breakTargetBlock(*this, gameplayContext);
}

void Player::placeBlockAtTarget(
    Gameplay::Context& gameplayContext
) {
    Gameplay::BlockInteractionSystem::placeBlockAtTarget(*this, gameplayContext);
}

void Player::dropSelectedItem(WorldStack& worldStack) {
    Gameplay::BlockInteractionSystem::dropSelectedItem(*this, worldStack);
}

void Player::spawnEnemyAtTarget(WorldStack& worldStack, EnemyType type) {
    Gameplay::BlockInteractionSystem::spawnEnemyAtTarget(*this, worldStack, type);
}

void Player::doRaycast(FractalWorld* world) {
    Gameplay::BlockInteractionSystem::doRaycast(*this, world);
}
