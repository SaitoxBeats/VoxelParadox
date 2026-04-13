// File: VoxelParadox.Client/src/Entities/player/player_persistence_system.hpp
// Purpose: captures and restores the aggregate player state behind the Player facade.
// Flow: Player keeps the public save/load API while this system owns the persistence mapping.

#pragma once

// 1. Local Project Modules
#include "player/player.hpp"

class PlayerPersistenceSystem {
public:
    static Player::PersistentState capture(const Player& player);
    static void apply(Player& player, const Player::PersistentState& state);
};
