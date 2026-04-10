// File: VoxelParadox.Client/src/Entities/items/item_catalog.hpp
// Purpose: exposes the stable item helper API used across gameplay, rendering, and save/load.
// Flow: delegates item lookups to the data-driven item catalog and registry while preserving the old call surface.

#pragma once

// 1. Standard Library
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// 2. Local Project Modules
#include "items/item_registry.hpp"
#include "world/block.hpp"

struct ItemCatalogEntry {
    ItemId value = 0;
    std::string stableId{};
    std::string folderName{};
};

namespace ItemCatalog {

const std::vector<ItemCatalogEntry>& entries();
const ItemCatalogEntry* findByValue(ItemId itemId);
const ItemCatalogEntry* findByStableId(std::string_view stableId);
bool tryResolve(std::string_view stableId, ItemId& outItemId);
ItemId require(std::string_view stableId);
std::size_t count();

} // namespace ItemCatalog

struct NamedItemIdRef {
    const char* stableId = "";

    operator ItemId() const;
};

struct ItemCountRef {
    operator ItemId() const;
};

namespace ItemIds {

inline constexpr NamedItemIdRef NONE{ "none" };
inline constexpr NamedItemIdRef AXE{ "axe" };
inline constexpr NamedItemIdRef PICKAXE{ "pickaxe" };
inline constexpr ItemCountRef COUNT{};

} // namespace ItemIds

inline const ItemDefinition& getItemDefinition(ItemId itemId) {
    return ItemRegistry::instance().definition(itemId);
}

InventoryItem makeInventoryBlock(BlockId blockType);
InventoryItem makeInventoryItem(ItemId itemId);

std::string normalizeItemId(std::string value);

const char* getItemId(ItemId itemId);
const char* getItemDisplayName(ItemId itemId);
std::uint32_t getItemCategories(ItemId itemId);
bool hasItemCategory(ItemId itemId, ItemCategory category);
const char* getItemCategoryDisplayName(ItemCategory category);
std::string getItemCategoryDisplayName(std::uint32_t categoryMask);
std::string getItemCategoryDisplayName(ItemId itemId);

const char* getInventoryItemId(const InventoryItem& item);
const char* getInventoryItemDisplayName(const InventoryItem& item);
std::string getInventoryItemCategoryDisplayName(const InventoryItem& item);

bool tryParseItemId(const std::string& rawValue, ItemId& outItemId);
bool tryParseInventoryItem(const std::string& rawValue, InventoryItem& outItem);

bool isToolItem(ItemId itemId);
bool isToolItem(const InventoryItem& item);
ToolDefinition getTool(ItemId itemId);
ToolDefinition getTool(const InventoryItem& item);
bool isToolEffectiveForBlock(const InventoryItem& toolItem, BlockId targetType);
float getToolAdjustedBreakTimeSeconds(
    const InventoryItem& toolItem,
    BlockId targetType,
    float baseBreakTimeSeconds
);
bool shouldDropBlockItemForTool(const InventoryItem& toolItem, BlockId brokenType);

bool isPlaceableInventoryItem(const InventoryItem& item);
int getItemStackLimit(ItemId itemId);
int getInventoryItemStackLimit(const InventoryItem& item);
const char* getItemTexturePath(ItemId itemId);
bool usesItemTexturePreview(const InventoryItem& item);

