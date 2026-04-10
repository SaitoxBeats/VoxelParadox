// gameplay_status.hpp
// Runtime-owned gameplay statistics tracking for the current world session.

#pragma once

// 1. Standard Library
#include <cstdint>

// 2. External Libraries

// 3. Project Headers
#include "enemies/enemy_types.hpp"

namespace GameplayStatus {

inline constexpr std::uint32_t kStatsVersion = 4;

struct PersistentState {
    double playtimeSeconds = 0.0;
    std::uint64_t deathCount = 0;
    std::uint64_t blocksBrokenCount = 0;
    std::uint64_t blocksPlacedCount = 0;
    std::uint64_t universesCreatedCount = 1;
    std::uint64_t currentUniversesCount = 0;
    std::uint64_t guySpawnCount = 0;
    std::uint64_t guyAttackCount = 0;
};

PersistentState sanitizePersistentState(const PersistentState& state);

class System {
public:
    static System& instance();

    void reset();
    void applyPersistentState(const PersistentState& state);
    PersistentState capturePersistentState() const;

    double playtimeSeconds() const;

    void addPlaytimeSeconds(double seconds);
    void setPlaytimeSeconds(double seconds);

    void recordDeath();
    void recordBlocksBroken(std::uint64_t count = 1);
    void recordBlocksPlaced(std::uint64_t count = 1);
    void recordUniverseCreated(std::uint64_t count = 1);
    void recordUniverseDeleted(std::uint64_t count = 1);
    void setCurrentUniversesCount(std::uint64_t count);
    void recordEnemySpawn(EnemyType type);
    void recordEnemyAttack(EnemyType type);

private:
    PersistentState state_{};
};

}  // namespace GameplayStatus
