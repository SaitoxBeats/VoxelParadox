// File: VoxelParadox.Client/src/Entities/items/item_registry.hpp
// Purpose: declares the data-driven item registry used by gameplay, rendering, and inventory systems.
// Flow: loads item definitions from data with a compiled fallback and exposes stable lookups.

#pragma once

// 1. Standard Library
#include <string>
#include <vector>

// 2. Local Project Modules
#include "items/item_behavior.hpp"
#include "items/item_types.hpp"

struct ItemWorldPreviewDefinition {
    bool useTextureSprite = true;
};

enum class ItemBehaviorKind : std::uint8_t {
    Default = 0,
    Tool,
    Declarative,
    LuaScript,
};

enum class ItemUseConditionType : std::uint8_t {
    None = 0,
    PlayerAlive,
    HasTarget,
    NoTarget,
    PortalTrackerAvailable,
    SelectedCountAtLeast,
};

struct ItemUseConditionDefinition {
    ItemUseConditionType type = ItemUseConditionType::None;
    int amount = 0;
    bool negate = false;
};

enum class ItemUseActionType : std::uint8_t {
    None = 0,
    PrintTerminal,
    OpenPortalTracker,
    PlaySound,
    HealPlayer,
    GiveItem,
    ConsumeItem,
};

struct ItemUseActionDefinition {
    ItemUseActionType type = ItemUseActionType::None;
    std::string stringValue{};
    InventoryItem item{};
    int amount = 0;
};

struct ItemUseDefinition {
    std::vector<ItemUseConditionDefinition> conditions{};
    float cooldownSeconds = 0.0f;
    int consumeCount = 0;
    std::vector<ItemUseActionDefinition> actions{};

    bool empty() const {
        return conditions.empty() &&
            cooldownSeconds <= 0.0f &&
            consumeCount <= 0 &&
            actions.empty();
    }
};

struct ItemScriptDefinition {
    bool enabled = false;
    std::string relativePath{ "script.lua" };
    std::string source{};
    std::string loadError{};
};

struct ItemDefinition {
    ItemId idValue = 0;
    std::string id{};
    std::string displayName{};
    std::uint32_t categoryMask = ITEM_CATEGORY_NONE;
    int stackLimit = 64;
    std::string iconAssetPath{};
    ItemBehaviorKind behaviorKind = ItemBehaviorKind::Default;
    std::string behaviorId{ "default" };
    const ItemBehavior* behavior = nullptr;
    bool hasToolDefinition = false;
    ToolDefinition tool{};
    ItemWorldPreviewDefinition worldPreview{};
    ItemUseDefinition onUse{};
    ItemScriptDefinition script{};
};

class ItemRegistry {
public:
    static const ItemRegistry& instance();
    static ItemRegistry& mutableInstance();

    const ItemDefinition& definition(ItemId itemId) const;
    const std::vector<ItemDefinition>& definitions() const;

    bool tryParseId(const std::string& rawValue, ItemId& outItemId) const;
    void reload();

private:
    ItemRegistry();

    std::vector<ItemDefinition> definitions_{};
};
