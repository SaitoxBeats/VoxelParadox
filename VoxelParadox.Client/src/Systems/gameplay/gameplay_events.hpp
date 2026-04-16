// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_events.hpp
// Purpose: defines the lightweight gameplay event stream used by runtime orchestration.
// Flow: gameplay producers enqueue events, and the runtime loop drains them to update shared systems.

#pragma once

// 1. Standard Library
#include <cstdint>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "items/item_types.hpp"

namespace Gameplay {

enum class EventType {
    BlockBroken,
    BlockPlaced,
    ItemAcquired,
    ItemCollected,
    PlayerExperienceChanged,
    PlayerDied,
    PlayerRespawned,
    PlayerLevelUp,
    PortalCreated,
    UniverseEntered,
    UniverseExited,
};

struct Event {
    EventType type = EventType::BlockBroken;
    glm::ivec3 blockPosition{ 0 };
    glm::ivec3 blockNormal{ 0 };
    BlockId blockType = BlockIds::AIR;
    bool countStats = true;
    InventoryItem item{};
    int itemAmount = 0;
    float experiencePoints = 0.0f;
    float experienceEarned = 0.0f;
    int previousLevel = 0;
    int newLevel = 0;
    std::string textValue;
};

class EventQueue {
public:
    void clear() {
        events_.clear();
    }

    void emitBlockBroken(
        const glm::ivec3& blockPosition,
        BlockId blockType,
        bool countStats = true,
        const glm::ivec3& blockNormal = glm::ivec3(0)
    ) {
        Event event{};
        event.type = EventType::BlockBroken;
        event.blockPosition = blockPosition;
        event.blockNormal = blockNormal;
        event.blockType = blockType;
        event.countStats = countStats;
        events_.push_back(event);
    }

    void emitBlockPlaced(const glm::ivec3& blockPosition, BlockId blockType) {
        Event event{};
        event.type = EventType::BlockPlaced;
        event.blockPosition = blockPosition;
        event.blockType = blockType;
        events_.push_back(event);
    }

    void emitItemAcquired(const InventoryItem& item, int itemAmount) {
        Event event{};
        event.type = EventType::ItemAcquired;
        event.item = item;
        event.itemAmount = itemAmount;
        events_.push_back(event);
    }

    void emitItemCollected(const InventoryItem& item, int itemAmount) {
        Event event{};
        event.type = EventType::ItemCollected;
        event.item = item;
        event.itemAmount = itemAmount;
        events_.push_back(event);
    }

    void emitPlayerExperienceChanged(
        float experiencePoints,
        int experienceLevel,
        float experienceEarned = 0.0f
    ) {
        Event event{};
        event.type = EventType::PlayerExperienceChanged;
        event.experiencePoints = experiencePoints;
        event.experienceEarned = experienceEarned;
        event.newLevel = experienceLevel;
        events_.push_back(event);
    }

    void emitPlayerDied() {
        Event event{};
        event.type = EventType::PlayerDied;
        events_.push_back(event);
    }

    void emitPlayerRespawned() {
        Event event{};
        event.type = EventType::PlayerRespawned;
        events_.push_back(event);
    }

    void emitPlayerLevelUp(int previousLevel, int newLevel) {
        Event event{};
        event.type = EventType::PlayerLevelUp;
        event.previousLevel = previousLevel;
        event.newLevel = newLevel;
        events_.push_back(event);
    }

    void emitPortalCreated(
        const glm::ivec3& blockPosition,
        const glm::ivec3& blockNormal = glm::ivec3(0)
    ) {
        Event event{};
        event.type = EventType::PortalCreated;
        event.blockPosition = blockPosition;
        event.blockNormal = blockNormal;
        events_.push_back(event);
    }

    void emitUniverseEntered(
        const glm::ivec3& portalBlock,
        const glm::ivec3& portalNormal,
        const std::string& nextBiomePresetId = {}
    ) {
        Event event{};
        event.type = EventType::UniverseEntered;
        event.blockPosition = portalBlock;
        event.blockNormal = portalNormal;
        event.textValue = nextBiomePresetId;
        events_.push_back(event);
    }

    void emitUniverseExited(
        const glm::ivec3& portalBlock,
        const glm::ivec3& portalNormal
    ) {
        Event event{};
        event.type = EventType::UniverseExited;
        event.blockPosition = portalBlock;
        event.blockNormal = portalNormal;
        events_.push_back(event);
    }

    std::vector<Event> drain() {
        std::vector<Event> drainedEvents;
        drainedEvents.swap(events_);
        return drainedEvents;
    }

private:
    std::vector<Event> events_{};
};

}  // namespace Gameplay
