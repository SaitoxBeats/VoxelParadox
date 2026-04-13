// File: VoxelParadox.Client/src/Entities/player/player_interaction_tuning.hpp
// Purpose: centralizes stable player interaction tuning shared by gameplay systems.
// Flow: Player and block interaction systems use these values without keeping them as Player internals.

#pragma once

struct PlayerInteractionTuning {
    static constexpr float kBreakHitRepeatInterval = 0.23f;
    static constexpr float kDroppedItemThrowSpeed = 3.75f;
    static constexpr float kDroppedItemSpawnDistance = 1.55f;
    static constexpr float kDroppedItemPickupDelay = 0.35f;
};
