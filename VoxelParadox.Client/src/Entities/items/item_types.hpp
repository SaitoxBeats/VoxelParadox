// File: VoxelParadox.Client/src/Entities/items/item_types.hpp
// Purpose: declares the stable low-level item ids, categories, and inventory item structs.
// Flow: consumed by the item registry, item facade, renderer, and gameplay systems.

#pragma once

// 1. Standard Library
#include <cstdint>

// 2. Local Project Modules
#include "world/block_catalog.hpp"
#include "world/block_types.hpp"

using ItemId = std::uint16_t;

enum class ItemCategory : std::uint32_t {
    NONE = 0u,
    TOOL = 1u << 0,
    UTILITY = 1u << 1,
    MATERIAL = 1u << 2,
    QUEST = 1u << 3,
};

inline constexpr std::uint32_t ITEM_CATEGORY_NONE =
    static_cast<std::uint32_t>(ItemCategory::NONE);
inline constexpr std::uint32_t ITEM_CATEGORY_TOOL =
    static_cast<std::uint32_t>(ItemCategory::TOOL);
inline constexpr std::uint32_t ITEM_CATEGORY_UTILITY =
    static_cast<std::uint32_t>(ItemCategory::UTILITY);
inline constexpr std::uint32_t ITEM_CATEGORY_MATERIAL =
    static_cast<std::uint32_t>(ItemCategory::MATERIAL);
inline constexpr std::uint32_t ITEM_CATEGORY_QUEST =
    static_cast<std::uint32_t>(ItemCategory::QUEST);

enum class InventoryItemKind : std::uint8_t {
    NONE = 0,
    BLOCK,
    ITEM
};

struct ToolDefinition {
    std::uint32_t effectiveTags = BLOCK_TAG_NONE;
    float efficiency = 1.0f;
};

struct InventoryItem {
    InventoryItemKind kind = InventoryItemKind::NONE;
    BlockId blockType = BlockIds::AIR;
    ItemId itemId = 0;

    bool empty() const {
        return kind == InventoryItemKind::NONE;
    }

    bool isBlock() const {
        return kind == InventoryItemKind::BLOCK && blockType != BlockIds::AIR;
    }

    bool isItem() const {
        return kind == InventoryItemKind::ITEM && itemId != 0;
    }
};

inline bool operator==(const InventoryItem& lhs, const InventoryItem& rhs) {
    return lhs.kind == rhs.kind &&
        lhs.blockType == rhs.blockType &&
        lhs.itemId == rhs.itemId;
}

inline bool operator!=(const InventoryItem& lhs, const InventoryItem& rhs) {
    return !(lhs == rhs);
}

