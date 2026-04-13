// File: VoxelParadox.Client/src/Systems/gameplay/block_interaction_system.hpp
// Purpose: centralizes player block interaction orchestration behind a reusable gameplay system.
// Flow: Player forwards interaction entry points here, and the system mutates Player state through its facade/friend access.

#pragma once

// 1. Local Project Modules
#include "enemies/enemy_types.hpp"

class FractalWorld;
class Player;
class WorldStack;

namespace Gameplay {

struct Context;

class BlockInteractionSystem {
public:
    static void handleBlockInteraction(
        Player& player,
        Context& gameplayContext
    );

    static void updateBlockBreaking(
        Player& player,
        Context& gameplayContext
    );

    static void breakTargetBlock(
        Player& player,
        Context& gameplayContext
    );

    static void placeBlockAtTarget(
        Player& player,
        Context& gameplayContext
    );

    static void dropSelectedItem(
        Player& player,
        WorldStack& worldStack
    );

    static void dropHeldItem(
        Player& player,
        WorldStack& worldStack
    );

    static void spawnEnemyAtTarget(
        Player& player,
        WorldStack& worldStack,
        EnemyType type
    );

    static void doRaycast(
        Player& player,
        FractalWorld* world
    );

private:
    static bool tryTargetOverlappingBodyBlock(
        Player& player,
        FractalWorld* world
    );
};

}  // namespace Gameplay
