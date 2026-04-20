// File: VoxelParadox.Client/src/Entities/items/item_catalog.cpp
// Purpose: loads the stable item id catalog and implements the compatibility item helper API.
// Flow: resolves named item ids from data first, then routes gameplay and renderer queries through the registry.

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

// 2. Third-party Libraries
#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "items/item_catalog.hpp"

#include "client_assets.hpp"
#include "path/app_paths.hpp"

namespace {

using json = nlohmann::json;

std::vector<ItemCatalogEntry> makeFallbackEntries() {
    return {
        { 0, "none", "none" },
        { 1, "axe", "axe" },
        { 2, "pickaxe", "pickaxe" },
        { 3, "portal_tracker", "portal_tracker" }
    };
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool validateEntries(const std::vector<ItemCatalogEntry>& entries) {
    if (entries.empty()) {
        return false;
    }

    std::vector<ItemCatalogEntry> sorted = entries;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const ItemCatalogEntry& left, const ItemCatalogEntry& right) {
            return left.value < right.value;
        }
    );

    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (sorted[index].value != static_cast<ItemId>(index)) {
            return false;
        }

        if (sorted[index].stableId.empty()) {
            return false;
        }
    }

    return true;
}

std::vector<ItemCatalogEntry> loadEntries() {
    std::vector<ItemCatalogEntry> fallback = makeFallbackEntries();
    const std::filesystem::path registryPath =
        AppPaths::assetsRoot() / "Models" / "Items" / "registry.json";
    const std::string source = readTextFile(registryPath);
    if (source.empty()) {
        return fallback;
    }

    const json root = json::parse(source, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        std::printf("[Items] Failed to parse %s. Using fallback item id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    const json entriesValue = root.value("entries", json::array());
    if (!entriesValue.is_array()) {
        std::printf("[Items] Invalid item registry schema in %s. Using fallback item id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    std::vector<ItemCatalogEntry> loadedEntries;
    loadedEntries.reserve(entriesValue.size());

    for (const json& entryValue : entriesValue) {
        if (!entryValue.is_object()) {
            continue;
        }

        if (!entryValue.contains("value") || !entryValue["value"].is_number_unsigned() ||
            !entryValue.contains("id") || !entryValue["id"].is_string()) {
            continue;
        }

        ItemCatalogEntry entry{};
        entry.value = entryValue["value"].get<ItemId>();
        entry.stableId = normalizeItemId(entryValue["id"].get<std::string>());
        entry.folderName = entryValue.value("folder", entry.stableId);
        if (entry.folderName.empty()) {
            entry.folderName = entry.stableId;
        }

        loadedEntries.push_back(std::move(entry));
    }

    if (!validateEntries(loadedEntries)) {
        std::printf("[Items] Non-contiguous or invalid item ids in %s. Using fallback item id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    return loadedEntries;
}

} // namespace

namespace ItemCatalog {

const std::vector<ItemCatalogEntry>& entries() {
    static const std::vector<ItemCatalogEntry> kEntries = loadEntries();
    return kEntries;
}

const ItemCatalogEntry* findByValue(ItemId itemId) {
    const auto& catalog = entries();
    const std::size_t index = static_cast<std::size_t>(itemId);
    if (index >= catalog.size()) {
        return nullptr;
    }

    return &catalog[index];
}

const ItemCatalogEntry* findByStableId(std::string_view stableId) {
    const std::string normalized = normalizeItemId(std::string(stableId));
    const auto& catalog = entries();
    for (const ItemCatalogEntry& entry : catalog) {
        if (entry.stableId == normalized) {
            return &entry;
        }
    }

    return nullptr;
}

bool tryResolve(std::string_view stableId, ItemId& outItemId) {
    const ItemCatalogEntry* entry = findByStableId(stableId);
    if (!entry) {
        outItemId = entries().front().value;
        return false;
    }

    outItemId = entry->value;
    return true;
}

ItemId require(std::string_view stableId) {
    ItemId itemId = entries().front().value;
    tryResolve(stableId, itemId);
    return itemId;
}

std::size_t count() {
    return entries().size();
}

} // namespace ItemCatalog

NamedItemIdRef::operator ItemId() const {
    return ItemCatalog::require(stableId);
}

ItemCountRef::operator ItemId() const {
    return static_cast<ItemId>(ItemCatalog::count());
}

InventoryItem makeInventoryBlock(BlockId blockType) {
    if (blockType == BlockIds::AIR || blockType == BlockIds::COUNT) {
        return {};
    }

    return { InventoryItemKind::BLOCK, blockType, 0 };
}

InventoryItem makeInventoryItem(ItemId itemId) {
    if (itemId == ItemIds::NONE || itemId == ItemIds::COUNT) {
        return {};
    }

    return { InventoryItemKind::ITEM, BlockIds::AIR, itemId };
}

std::string normalizeItemId(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            if (ch == ' ' || ch == '-') {
                return static_cast<char>('_');
            }
            return static_cast<char>(std::tolower(ch));
        }
    );
    return value;
}

const char* getItemId(ItemId itemId) {
    return getItemDefinition(itemId).id.c_str();
}

const char* getItemDisplayName(ItemId itemId) {
    return getItemDefinition(itemId).displayName.c_str();
}

std::uint32_t getItemCategories(ItemId itemId) {
    return getItemDefinition(itemId).categoryMask;
}

bool hasItemCategory(ItemId itemId, ItemCategory category) {
    return (getItemCategories(itemId) & static_cast<std::uint32_t>(category)) != 0u;
}

const char* getItemCategoryDisplayName(ItemCategory category) {
    switch (category) {
    case ItemCategory::TOOL:
        return "Tool";
    case ItemCategory::UTILITY:
        return "Utility";
    case ItemCategory::MATERIAL:
        return "Material";
    case ItemCategory::QUEST:
        return "Quest";
    case ItemCategory::FOOD:
        return "Food";
    case ItemCategory::NONE:
    default:
        return "None";
    }
}

std::string getItemCategoryDisplayName(std::uint32_t categoryMask) {
    std::string result;

    const auto appendCategory = [&result, categoryMask](ItemCategory category) {
        const std::uint32_t mask = static_cast<std::uint32_t>(category);
        if ((categoryMask & mask) == 0u) {
            return;
        }

        if (!result.empty()) {
            result += " / ";
        }

        result += getItemCategoryDisplayName(category);
    };

    appendCategory(ItemCategory::TOOL);
    appendCategory(ItemCategory::UTILITY);
    appendCategory(ItemCategory::MATERIAL);
    appendCategory(ItemCategory::QUEST);
    appendCategory(ItemCategory::FOOD);

    if (result.empty()) {
        result = getItemCategoryDisplayName(ItemCategory::NONE);
    }

    return result;
}

std::string getItemCategoryDisplayName(ItemId itemId) {
    return getItemCategoryDisplayName(getItemCategories(itemId));
}

const char* getInventoryItemId(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockId(item.blockType);
    }

    if (item.isItem()) {
        return getItemId(item.itemId);
    }

    return "none";
}

const char* getInventoryItemDisplayName(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockDisplayName(item.blockType);
    }

    if (item.isItem()) {
        return getItemDisplayName(item.itemId);
    }

    return "None";
}

std::string getInventoryItemCategoryDisplayName(const InventoryItem& item) {
    if (item.isBlock()) {
        return getBlockCategoryDisplayName(item.blockType);
    }

    if (item.isItem()) {
        return getItemCategoryDisplayName(item.itemId);
    }

    return {};
}

bool tryParseItemId(const std::string& rawValue, ItemId& outItemId) {
    return ItemRegistry::instance().tryParseId(rawValue, outItemId);
}

bool tryParseInventoryItem(const std::string& rawValue, InventoryItem& outItem) {
    constexpr std::string_view kBlockPrefix = "block:";
    constexpr std::string_view kItemPrefix = "item:";

    if (rawValue.rfind(kBlockPrefix, 0) == 0) {
        BlockId parsedBlock = BlockIds::AIR;
        if (tryParseBlockType(rawValue.substr(kBlockPrefix.size()), parsedBlock) &&
            parsedBlock != BlockIds::AIR) {
            outItem = makeInventoryBlock(parsedBlock);
            return true;
        }

        outItem = {};
        return false;
    }

    if (rawValue.rfind(kItemPrefix, 0) == 0) {
        ItemId parsedItem = ItemIds::NONE;
        if (tryParseItemId(rawValue.substr(kItemPrefix.size()), parsedItem) &&
            parsedItem != ItemIds::NONE) {
            outItem = makeInventoryItem(parsedItem);
            return true;
        }

        outItem = {};
        return false;
    }

    BlockId parsedBlock = BlockIds::AIR;
    if (tryParseBlockType(rawValue, parsedBlock) && parsedBlock != BlockIds::AIR) {
        outItem = makeInventoryBlock(parsedBlock);
        return true;
    }

    ItemId parsedItem = ItemIds::NONE;
    if (tryParseItemId(rawValue, parsedItem) && parsedItem != ItemIds::NONE) {
        outItem = makeInventoryItem(parsedItem);
        return true;
    }

    outItem = {};
    return false;
}

bool isToolItem(ItemId itemId) {
    return getItemDefinition(itemId).behavior &&
        getItemDefinition(itemId).behavior->isTool(getItemDefinition(itemId));
}

bool isToolItem(const InventoryItem& item) {
    return item.isItem() && isToolItem(item.itemId);
}

ToolDefinition getTool(ItemId itemId) {
    const ItemDefinition& definition = getItemDefinition(itemId);
    if (!definition.behavior) {
        return {};
    }

    return definition.behavior->getToolDefinition(definition);
}

ToolDefinition getTool(const InventoryItem& item) {
    return item.isItem() ? getTool(item.itemId) : ToolDefinition{};
}

bool isToolEffectiveForBlock(const InventoryItem& toolItem, BlockId targetType) {
    const ToolDefinition tool = getTool(toolItem);
    return tool.effectiveTags != BLOCK_TAG_NONE &&
        hasBlockTag(targetType, tool.effectiveTags);
}

float getToolAdjustedBreakTimeSeconds(
    const InventoryItem& toolItem,
    BlockId targetType,
    float baseBreakTimeSeconds
) {
    if (baseBreakTimeSeconds <= 0.0f) {
        return baseBreakTimeSeconds;
    }

    const ToolDefinition tool = getTool(toolItem);
    const bool isEffective = tool.effectiveTags != BLOCK_TAG_NONE &&
        hasBlockTag(targetType, tool.effectiveTags);

    const float efficiency = isEffective ? std::max(tool.efficiency, 1.0f) : 1.0f;
    return baseBreakTimeSeconds / efficiency;
}

bool shouldDropBlockItemForTool(const InventoryItem& toolItem, BlockId brokenType) {
    if (!hasConfiguredBlockDropItem(brokenType)) {
        return false;
    }

    if (canBlockDropWithoutTool(brokenType)) {
        return true;
    }

    return isToolEffectiveForBlock(toolItem, brokenType);
}

bool isPlaceableInventoryItem(const InventoryItem& item) {
    return item.isBlock() && isPlaceableBlockType(item.blockType);
}

int getItemStackLimit(ItemId itemId) {
    if (itemId == ItemIds::NONE) {
        return 0;
    }

    const ItemDefinition& definition = getItemDefinition(itemId);
    if (!definition.behavior) {
        return std::max(definition.stackLimit, 1);
    }

    return std::max(definition.behavior->getStackLimit(definition), 1);
}

int getInventoryItemStackLimit(const InventoryItem& item) {
    if (item.isItem()) {
        return getItemStackLimit(item.itemId);
    }

    return 64;
}

const char* getItemTexturePath(ItemId itemId) {
    const ItemDefinition& definition = getItemDefinition(itemId);
    return definition.iconAssetPath.empty() ? nullptr : definition.iconAssetPath.c_str();
}

bool usesItemTexturePreview(const InventoryItem& item) {
    return item.isItem() && getItemTexturePath(item.itemId) != nullptr;
}
