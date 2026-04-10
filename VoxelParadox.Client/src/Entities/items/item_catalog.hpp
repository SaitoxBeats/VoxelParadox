// item_catalog.hpp
// Unity mental model: Global static definitions for items, tools, and inventory types.
// Contains type enumerations, data structures, and static helper functions.

#pragma once

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <cstdint>
#include <string>

// 2. Local Project Modules
#include "client_assets.hpp"
#include "world/block.hpp"

#pragma endregion

#pragma region 1. Core Enumerations
// --- 1. Core Enumerations ---

enum class ItemType : std::uint8_t {
    NONE = 0,
    AXE,
    PICKAXE,
    COUNT
};

enum class InventoryItemKind : std::uint8_t {
    NONE = 0,
    BLOCK,
    ITEM
};

#pragma endregion

#pragma region 2. Core Data Structures
// --- 2. Core Data Structures ---

struct Tool {
    std::uint32_t effectiveTags = BLOCK_TAG_NONE;
    float efficiency = 1.0f;
};

struct InventoryItem {
    InventoryItemKind kind = InventoryItemKind::NONE;
    BlockId blockType = BlockIds::AIR;
    ItemType itemType = ItemType::NONE;

    bool empty() const {
        return kind == InventoryItemKind::NONE;
    }

    bool isBlock() const {
        return kind == InventoryItemKind::BLOCK && blockType != BlockIds::AIR;
    }

    bool isItem() const {
        return kind == InventoryItemKind::ITEM && itemType != ItemType::NONE;
    }
};

inline bool operator==(const InventoryItem& lhs, const InventoryItem& rhs) {
    return lhs.kind == rhs.kind &&
        lhs.blockType == rhs.blockType &&
        lhs.itemType == rhs.itemType;
}

inline bool operator!=(const InventoryItem& lhs, const InventoryItem& rhs) {
    return !(lhs == rhs);
}

#pragma endregion

#pragma region 3. Constructors & Factories
// --- 3. Constructors & Factories ---

inline InventoryItem makeInventoryBlock(BlockId blockType) {
    if (blockType == BlockIds::AIR || blockType == BlockIds::COUNT) {
        return {};
    }
    return { InventoryItemKind::BLOCK, blockType, ItemType::NONE };
}

inline InventoryItem makeInventoryItem(ItemType itemType) {
    if (itemType == ItemType::NONE || itemType == ItemType::COUNT) {
        return {};
    }
    return { InventoryItemKind::ITEM, BlockIds::AIR, itemType };
}

#pragma endregion

#pragma region 4. Parsing & Formatting
// --- 4. Parsing & Formatting ---

inline std::string normalizeItemId(std::string value) {
    return normalizeBlockId(value);
}

inline const char* getItemId(ItemType type) {
    switch (type) {
    case ItemType::NONE:    return "none";
    case ItemType::AXE:     return "axe";
    case ItemType::PICKAXE: return "pickaxe";
    case ItemType::COUNT:
    default:                return "unknown";
    }
}

inline const char* getItemDisplayName(ItemType type) {
    switch (type) {
    case ItemType::AXE:     return "Axe";
    case ItemType::PICKAXE: return "Pickaxe";
    case ItemType::NONE:    return "None";
    case ItemType::COUNT:
    default:                return "Unknown";
    }
}

inline const char* getInventoryItemId(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockId(item.blockType);
    }
    if (item.isItem()) {
        return getItemId(item.itemType);
    }
    return "none";
}

inline const char* getInventoryItemDisplayName(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockDisplayName(item.blockType);
    }
    if (item.isItem()) {
        return getItemDisplayName(item.itemType);
    }
    return "None";
}

inline std::string getInventoryItemCategoryDisplayName(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockCategoryDisplayName(item.blockType);
    }
    return {};
}

inline bool tryParseItemType(const std::string& rawValue, ItemType& outType) {
    const std::string value = normalizeItemId(rawValue);

    for (int index = 0; index < static_cast<int>(ItemType::COUNT); index++) {
        const ItemType type = static_cast<ItemType>(index);
        if (value == getItemId(type)) {
            outType = type;
            return true;
        }
    }

    outType = ItemType::NONE;
    return false;
}

inline bool tryParseInventoryItem(const std::string& rawValue, InventoryItem& outItem) {
    BlockId parsedBlock = BlockIds::AIR;

    if (tryParseBlockType(rawValue, parsedBlock) && parsedBlock != BlockIds::AIR) {
        outItem = makeInventoryBlock(parsedBlock);
        return true;
    }

    ItemType parsedItem = ItemType::NONE;

    if (tryParseItemType(rawValue, parsedItem) && parsedItem != ItemType::NONE) {
        outItem = makeInventoryItem(parsedItem);
        return true;
    }

    outItem = {};
    return false;
}

#pragma endregion

#pragma region 5. Tool Behaviors & Mechanics
// --- 5. Tool Behaviors & Mechanics ---

inline bool isToolItem(ItemType type) {
    return type == ItemType::AXE || type == ItemType::PICKAXE;
}

inline bool isToolItem(const InventoryItem& item) {
    return item.isItem() && isToolItem(item.itemType);
}

inline Tool getTool(ItemType type) {
    switch (type) {
    case ItemType::AXE:     return { BLOCK_TAG_MINEABLE_WITH_AXE, 3.0f };
    case ItemType::PICKAXE: return { BLOCK_TAG_MINEABLE_WITH_PICKAXE, 3.0f };
    case ItemType::NONE:
    case ItemType::COUNT:
    default:                return {};
    }
}

inline Tool getTool(const InventoryItem& item) {
    return item.isItem() ? getTool(item.itemType) : Tool{};
}

inline bool isToolEffectiveForBlock(const InventoryItem& toolItem, BlockId targetType) {
    const Tool tool = getTool(toolItem);

    return tool.effectiveTags != BLOCK_TAG_NONE &&
        hasBlockTag(targetType, tool.effectiveTags);
}

inline float getToolAdjustedBreakTimeSeconds(const InventoryItem& toolItem,
    BlockId targetType,
    float baseBreakTimeSeconds) {
    if (baseBreakTimeSeconds <= 0.0f) {
        return baseBreakTimeSeconds;
    }

    const Tool tool = getTool(toolItem);
    const bool isEffective = tool.effectiveTags != BLOCK_TAG_NONE &&
        hasBlockTag(targetType, tool.effectiveTags);

    const float efficiency = isEffective ? std::max(tool.efficiency, 1.0f) : 1.0f;

    return baseBreakTimeSeconds / efficiency;
}

inline bool shouldDropBlockItemForTool(const InventoryItem& toolItem, BlockId brokenType) {
    if (canBlockDropItem(brokenType)) {
        return true;
    }

    return brokenType == BlockIds::STONE &&
        toolItem.isItem() &&
        toolItem.itemType == ItemType::PICKAXE;
}

#pragma endregion

#pragma region 6. Inventory Limits & Properties
// --- 6. Inventory Limits & Properties ---

inline bool isPlaceableInventoryItem(const InventoryItem& item) {
    return item.isBlock() && isPlaceableBlockType(item.blockType);
}

inline int getItemStackLimit(ItemType type) {
    return isToolItem(type) ? 1 : 64;
}

inline int getInventoryItemStackLimit(const InventoryItem& item) {
    if (item.isItem()) {
        return getItemStackLimit(item.itemType);
    }
    return 64;
}

inline const char* getItemTexturePath(ItemType type) {
    switch (type) {
    case ItemType::AXE:     return ClientAssets::kAxeTexture;
    case ItemType::PICKAXE: return ClientAssets::kPickaxeTexture;
    default:                return nullptr;
    }
}

inline bool usesItemTexturePreview(const InventoryItem& item) {
    return item.isItem() && getItemTexturePath(item.itemType) != nullptr;
}

#pragma endregion
