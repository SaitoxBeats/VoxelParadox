// gameplay_status.cpp
// Central runtime gameplay statistics management for the current world session.

// 1. Standard Library
#include <algorithm>

// 2. External Libraries

// 3. Project Headers
#include "gameplay_status.hpp"

namespace GameplayStatus {

namespace {

}  // namespace

PersistentState sanitizePersistentState(const PersistentState& state) {
    PersistentState sanitized = state;
    sanitized.playtimeSeconds = std::max(0.0, sanitized.playtimeSeconds);
    sanitized.universesCreatedCount =
        std::max(sanitized.universesCreatedCount, sanitized.currentUniversesCount);
    return sanitized;
}

System& System::instance() {
    static System system;
    return system;
}

void System::reset() {
    state_ = sanitizePersistentState(PersistentState{});
}

void System::applyPersistentState(const PersistentState& state) {
    state_ = sanitizePersistentState(state);
}

PersistentState System::capturePersistentState() const {
    return sanitizePersistentState(state_);
}

double System::playtimeSeconds() const {
    return state_.playtimeSeconds;
}

void System::addPlaytimeSeconds(double seconds) {
    if (seconds <= 0.0) {
        return;
    }

    state_.playtimeSeconds = std::max(0.0, state_.playtimeSeconds + seconds);
}

void System::setPlaytimeSeconds(double seconds) {
    state_.playtimeSeconds = std::max(0.0, seconds);
}

void System::recordDeath() {
    ++state_.deathCount;
}

void System::recordBlocksBroken(std::uint64_t count) {
    state_.blocksBrokenCount += count;
}

void System::recordBlocksPlaced(std::uint64_t count) {
    state_.blocksPlacedCount += count;
}

void System::recordUniverseCreated(std::uint64_t count) {
    if (count == 0) {
        return;
    }

    state_.universesCreatedCount += count;
}

void System::recordUniverseDeleted(std::uint64_t count) {
    (void)count;
}

void System::setCurrentUniversesCount(std::uint64_t count) {
    state_.currentUniversesCount = count;
    state_.universesCreatedCount =
        std::max(state_.universesCreatedCount, state_.currentUniversesCount);
}

void System::recordEnemySpawn(EnemyType type) {
    if (type == EnemyType::Guy) {
        ++state_.guySpawnCount;
    }
}

void System::recordEnemyAttack(EnemyType type) {
    if (type == EnemyType::Guy) {
        ++state_.guyAttackCount;
    }
}

}  // namespace GameplayStatus
