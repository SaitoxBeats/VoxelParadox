// gameplay_status.cpp
// Central runtime gameplay statistics management for the current world session.

// 1. Standard Library
#include <algorithm>
#include <cmath>

// 2. External Libraries

// 3. Project Headers
#include "gameplay_status.hpp"

namespace GameplayStatus {

namespace {

void sanitizeCountMap(std::map<std::string, std::uint64_t>& counts) {
    for (auto it = counts.begin(); it != counts.end();) {
        if (it->first.empty()) {
            it = counts.erase(it);
            continue;
        }

        ++it;
    }
}

}  // namespace

PersistentState sanitizePersistentState(const PersistentState& state) {
    PersistentState sanitized = state;
    sanitized.playtimeSeconds = std::max(0.0, sanitized.playtimeSeconds);
    sanitized.playerXp =
        std::isfinite(sanitized.playerXp) ? std::max(0.0f, sanitized.playerXp) : 0.0f;
    sanitized.totalPlayerXpEarned =
        std::isfinite(sanitized.totalPlayerXpEarned)
            ? std::max(0.0, sanitized.totalPlayerXpEarned)
            : 0.0;
    sanitized.playerLevel = std::max(0, sanitized.playerLevel);
    sanitizeCountMap(sanitized.acquiredBlockCounts);
    sanitizeCountMap(sanitized.acquiredItemCounts);
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

float System::playerXp() const {
    return state_.playerXp;
}

double System::totalPlayerXpEarned() const {
    return state_.totalPlayerXpEarned;
}

int System::playerLevel() const {
    return state_.playerLevel;
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

void System::setPlayerXp(float xp) {
    state_.playerXp = std::isfinite(xp) ? std::max(0.0f, xp) : 0.0f;
}

void System::setPlayerLevel(int level) {
    state_.playerLevel = std::max(0, level);
}

void System::recordPlayerXpEarned(float amount) {
    if (!std::isfinite(amount) || amount <= 0.0f) {
        return;
    }

    state_.totalPlayerXpEarned += static_cast<double>(amount);
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

std::uint64_t System::recordBlockAcquired(const std::string& blockId, std::uint64_t count) {
    if (blockId.empty() || count == 0) {
        return blockId.empty() ? 0 : state_.acquiredBlockCounts[blockId];
    }

    std::uint64_t& total = state_.acquiredBlockCounts[blockId];
    total += count;
    return total;
}

std::uint64_t System::recordItemAcquired(const std::string& itemId, std::uint64_t count) {
    if (itemId.empty() || count == 0) {
        return itemId.empty() ? 0 : state_.acquiredItemCounts[itemId];
    }

    std::uint64_t& total = state_.acquiredItemCounts[itemId];
    total += count;
    return total;
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
