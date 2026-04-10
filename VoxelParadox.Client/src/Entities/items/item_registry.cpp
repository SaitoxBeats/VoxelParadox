// File: VoxelParadox.Client/src/Entities/items/item_registry.cpp
// Purpose: loads runtime item definitions from Assets/Items with a compiled fallback.
// Flow: catalog ids stay stable while gameplay and rendering read per-item metadata from data files.

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

// 2. Third-party Libraries
#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "items/item_registry.hpp"

#include "client_assets.hpp"
#include "items/item_catalog.hpp"
#include "path/app_paths.hpp"

namespace {

using json = nlohmann::json;

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string titleCaseFromId(const std::string& stableId) {
    std::string result;
    result.reserve(stableId.size());

    bool uppercaseNext = true;
    for (char ch : stableId) {
        if (ch == '_' || ch == '-') {
            result.push_back(' ');
            uppercaseNext = true;
            continue;
        }

        if (uppercaseNext) {
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            uppercaseNext = false;
            continue;
        }

        result.push_back(ch);
    }

    return result;
}

std::uint32_t parseItemCategoryName(const std::string& rawValue) {
    const std::string value = normalizeItemId(rawValue);
    if (value == "tool") {
        return ITEM_CATEGORY_TOOL;
    }
    if (value == "utility") {
        return ITEM_CATEGORY_UTILITY;
    }
    if (value == "material") {
        return ITEM_CATEGORY_MATERIAL;
    }
    if (value == "quest") {
        return ITEM_CATEGORY_QUEST;
    }
    return ITEM_CATEGORY_NONE;
}

std::uint32_t parseBlockTagName(const std::string& rawValue) {
    const std::string value = normalizeItemId(rawValue);
    if (value == "mineable_with_axe") {
        return BLOCK_TAG_MINEABLE_WITH_AXE;
    }
    if (value == "mineable_with_pickaxe") {
        return BLOCK_TAG_MINEABLE_WITH_PICKAXE;
    }
    return BLOCK_TAG_NONE;
}

ItemBehaviorKind parseBehaviorKindName(const std::string& rawValue) {
    const std::string value = normalizeItemId(rawValue);
    if (value == "tool") {
        return ItemBehaviorKind::Tool;
    }
    if (value == "declarative" || value == "portal_tracker") {
        return ItemBehaviorKind::Declarative;
    }
    if (value == "lua" || value == "script" || value == "lua_script") {
        return ItemBehaviorKind::LuaScript;
    }
    return ItemBehaviorKind::Default;
}

ItemUseConditionType parseItemUseConditionType(const std::string& rawValue) {
    const std::string value = normalizeItemId(rawValue);
    if (value == "player_alive") {
        return ItemUseConditionType::PlayerAlive;
    }
    if (value == "has_target") {
        return ItemUseConditionType::HasTarget;
    }
    if (value == "no_target") {
        return ItemUseConditionType::NoTarget;
    }
    if (value == "portal_tracker_available") {
        return ItemUseConditionType::PortalTrackerAvailable;
    }
    if (value == "selected_count_at_least") {
        return ItemUseConditionType::SelectedCountAtLeast;
    }
    return ItemUseConditionType::None;
}

ItemUseActionType parseItemUseActionType(const std::string& rawValue) {
    const std::string value = normalizeItemId(rawValue);
    if (value == "print_terminal") {
        return ItemUseActionType::PrintTerminal;
    }
    if (value == "open_portal_tracker") {
        return ItemUseActionType::OpenPortalTracker;
    }
    if (value == "play_sound") {
        return ItemUseActionType::PlaySound;
    }
    if (value == "heal_player") {
        return ItemUseActionType::HealPlayer;
    }
    if (value == "give_item") {
        return ItemUseActionType::GiveItem;
    }
    if (value == "consume_item") {
        return ItemUseActionType::ConsumeItem;
    }
    return ItemUseActionType::None;
}

std::uint32_t parseTagMask(const json& value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint32_t>();
    }

    if (!value.is_array()) {
        return BLOCK_TAG_NONE;
    }

    std::uint32_t mask = BLOCK_TAG_NONE;
    for (const json& entry : value) {
        if (!entry.is_string()) {
            continue;
        }
        mask |= parseBlockTagName(entry.get<std::string>());
    }

    return mask;
}

ItemDefinition makeGenericDefinition(const ItemCatalogEntry& entry) {
    ItemDefinition definition{};
    definition.idValue = entry.value;
    definition.id = entry.stableId;
    definition.displayName = titleCaseFromId(entry.stableId);
    definition.categoryMask = ITEM_CATEGORY_NONE;
    definition.stackLimit = entry.value == ItemIds::NONE ? 0 : 64;
    definition.behaviorKind = ItemBehaviorKind::Default;
    definition.behaviorId = "default";
    definition.behavior = &resolveItemBehavior(definition.behaviorId);
    return definition;
}

ItemDefinition makeFallbackDefinition(const ItemCatalogEntry& entry) {
    ItemDefinition definition = makeGenericDefinition(entry);

    if (entry.value == ItemIds::NONE) {
        definition.displayName = "None";
        definition.stackLimit = 0;
        return definition;
    }

    if (entry.value == ItemIds::AXE) {
        definition.displayName = "Axe";
        definition.categoryMask = ITEM_CATEGORY_TOOL;
        definition.stackLimit = 1;
        definition.iconAssetPath = ClientAssets::kAxeTexture;
        definition.behaviorKind = ItemBehaviorKind::Tool;
        definition.behaviorId = "tool";
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        definition.hasToolDefinition = true;
        definition.tool = { BLOCK_TAG_MINEABLE_WITH_AXE, 3.0f };
        return definition;
    }

    if (entry.value == ItemIds::PICKAXE) {
        definition.displayName = "Pickaxe";
        definition.categoryMask = ITEM_CATEGORY_TOOL;
        definition.stackLimit = 1;
        definition.iconAssetPath = ClientAssets::kPickaxeTexture;
        definition.behaviorKind = ItemBehaviorKind::Tool;
        definition.behaviorId = "tool";
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        definition.hasToolDefinition = true;
        definition.tool = { BLOCK_TAG_MINEABLE_WITH_PICKAXE, 3.0f };
        return definition;
    }

    if (entry.stableId == "portal_tracker") {
        definition.displayName = "Portal Tracker";
        definition.categoryMask = ITEM_CATEGORY_UTILITY;
        definition.stackLimit = 1;
        definition.iconAssetPath = "Assets/Textures/Items/portal_tracker.png";
        definition.behaviorKind = ItemBehaviorKind::Declarative;
        definition.behaviorId = "declarative";
        definition.onUse.actions.push_back({
            ItemUseActionType::OpenPortalTracker,
            {},
            {},
            0
        });
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        return definition;
    }

    return definition;
}

ItemUseConditionDefinition parseItemUseCondition(const json& value) {
    ItemUseConditionDefinition condition{};
    if (!value.is_object()) {
        return condition;
    }

    if (value.contains("type") && value["type"].is_string()) {
        condition.type = parseItemUseConditionType(value["type"].get<std::string>());
    }

    if (value.contains("amount") && value["amount"].is_number_integer()) {
        condition.amount = std::max(value["amount"].get<int>(), 0);
    }

    if (value.contains("not") && value["not"].is_boolean()) {
        condition.negate = value["not"].get<bool>();
    }

    return condition;
}

ItemUseActionDefinition parseItemUseAction(const json& value) {
    ItemUseActionDefinition action{};
    if (!value.is_object()) {
        return action;
    }

    if (value.contains("type") && value["type"].is_string()) {
        action.type = parseItemUseActionType(value["type"].get<std::string>());
    }

    if (value.contains("message") && value["message"].is_string()) {
        action.stringValue = value["message"].get<std::string>();
    }

    if (value.contains("event") && value["event"].is_string()) {
        action.stringValue = value["event"].get<std::string>();
    }

    if (value.contains("amount") && value["amount"].is_number_integer()) {
        action.amount = std::max(value["amount"].get<int>(), 0);
    }

    if (value.contains("item") && value["item"].is_string()) {
        InventoryItem parsedItem{};
        if (tryParseInventoryItem(value["item"].get<std::string>(), parsedItem)) {
            action.item = parsedItem;
        }
    }

    return action;
}

void applyLegacyBehaviorJson(const std::string& behaviorId, ItemDefinition& definition) {
    const std::string normalizedBehaviorId = normalizeItemId(behaviorId);
    definition.behaviorId = normalizedBehaviorId;
    definition.behaviorKind = parseBehaviorKindName(normalizedBehaviorId);

    if (normalizedBehaviorId == "portal_tracker" && definition.onUse.empty()) {
        definition.onUse.actions.push_back({
            ItemUseActionType::OpenPortalTracker,
            {},
            {},
            0
        });
    }
}

void applyOnUseJson(const json& value, ItemDefinition& definition) {
    if (!value.is_object()) {
        return;
    }

    if (value.contains("conditions") && value["conditions"].is_array()) {
        definition.onUse.conditions.clear();
        for (const json& conditionValue : value["conditions"]) {
            const ItemUseConditionDefinition condition =
                parseItemUseCondition(conditionValue);
            if (condition.type == ItemUseConditionType::None) {
                continue;
            }
            definition.onUse.conditions.push_back(condition);
        }
    }

    if (value.contains("cooldown") && value["cooldown"].is_number()) {
        definition.onUse.cooldownSeconds =
            std::max(value["cooldown"].get<float>(), 0.0f);
    }

    if (value.contains("consume")) {
        if (value["consume"].is_boolean()) {
            definition.onUse.consumeCount = value["consume"].get<bool>() ? 1 : 0;
        }
        else if (value["consume"].is_number_integer()) {
            definition.onUse.consumeCount =
                std::max(value["consume"].get<int>(), 0);
        }
    }

    if (value.contains("actions") && value["actions"].is_array()) {
        definition.onUse.actions.clear();
        for (const json& actionValue : value["actions"]) {
            const ItemUseActionDefinition action = parseItemUseAction(actionValue);
            if (action.type == ItemUseActionType::None) {
                continue;
            }
            definition.onUse.actions.push_back(action);
        }
    }
}

void applyScriptJson(
    const json& value,
    const std::filesystem::path& definitionRoot,
    ItemDefinition& definition
) {
    if (value.is_boolean()) {
        definition.script.enabled = value.get<bool>();
    }
    else if (value.is_object()) {
        definition.script.enabled = value.value("enabled", true);

        if (value.contains("path") && value["path"].is_string()) {
            definition.script.relativePath = value["path"].get<std::string>();
        }
    }

    if (!definition.script.enabled) {
        return;
    }

    const std::filesystem::path scriptPath =
        definitionRoot / definition.script.relativePath;
    definition.script.source = readTextFile(scriptPath);

    if (definition.script.source.empty()) {
        definition.script.loadError =
            "Failed to read item script: " + scriptPath.string();
    }
}

void finalizeBehaviorDefinition(ItemDefinition& definition) {
    if (definition.behaviorKind == ItemBehaviorKind::Tool) {
        definition.behaviorId = "tool";
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        return;
    }

    if (definition.script.enabled) {
        definition.behaviorKind = ItemBehaviorKind::LuaScript;
        definition.behaviorId = "lua";
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        return;
    }

    if (!definition.onUse.empty()) {
        definition.behaviorKind = ItemBehaviorKind::Declarative;
        definition.behaviorId = "declarative";
        definition.behavior = &resolveItemBehavior(definition.behaviorId);
        return;
    }

    definition.behaviorKind = ItemBehaviorKind::Default;
    definition.behaviorId = "default";
    definition.behavior = &resolveItemBehavior(definition.behaviorId);
}

void applyDefinitionJson(
    const json& value,
    const std::filesystem::path& definitionRoot,
    ItemDefinition& definition
) {
    if (!value.is_object()) {
        finalizeBehaviorDefinition(definition);
        return;
    }

    if (value.contains("id") && value["id"].is_string()) {
        definition.id = normalizeItemId(value["id"].get<std::string>());
    }

    if (value.contains("display_name") && value["display_name"].is_string()) {
        definition.displayName = value["display_name"].get<std::string>();
    }

    if (value.contains("categories") && value["categories"].is_array()) {
        definition.categoryMask = ITEM_CATEGORY_NONE;
        for (const json& categoryValue : value["categories"]) {
            if (!categoryValue.is_string()) {
                continue;
            }
            definition.categoryMask |= parseItemCategoryName(categoryValue.get<std::string>());
        }
    }

    if (value.contains("stack_limit") && value["stack_limit"].is_number_integer()) {
        definition.stackLimit = std::max(value["stack_limit"].get<int>(), 0);
    }

    if (value.contains("icon_asset") && value["icon_asset"].is_string()) {
        definition.iconAssetPath = value["icon_asset"].get<std::string>();
    }

    if (value.contains("behavior") && value["behavior"].is_string()) {
        applyLegacyBehaviorJson(value["behavior"].get<std::string>(), definition);
    }

    if (value.contains("tool") && value["tool"].is_object()) {
        const json& toolValue = value["tool"];
        definition.hasToolDefinition = true;

        if (toolValue.contains("effective_tags")) {
            definition.tool.effectiveTags = parseTagMask(toolValue["effective_tags"]);
        }

        if (toolValue.contains("efficiency") && toolValue["efficiency"].is_number()) {
            definition.tool.efficiency = toolValue["efficiency"].get<float>();
        }
    }

    if (value.contains("world_preview") && value["world_preview"].is_object()) {
        const json& previewValue = value["world_preview"];
        if (previewValue.contains("use_texture_sprite") &&
            previewValue["use_texture_sprite"].is_boolean()) {
            definition.worldPreview.useTextureSprite =
                previewValue["use_texture_sprite"].get<bool>();
        }
    }

    if (value.contains("on_use")) {
        applyOnUseJson(value["on_use"], definition);
    }

    if (value.contains("script")) {
        applyScriptJson(value["script"], definitionRoot, definition);
    }

    finalizeBehaviorDefinition(definition);
}

std::filesystem::path definitionPathFor(const ItemCatalogEntry& entry) {
    return AppPaths::assetsRoot() / "Items" / entry.folderName / "item.json";
}

std::vector<ItemDefinition> loadDefinitions() {
    std::vector<ItemDefinition> definitions;
    definitions.reserve(ItemCatalog::count());

    for (const ItemCatalogEntry& entry : ItemCatalog::entries()) {
        ItemDefinition definition = makeFallbackDefinition(entry);
        const std::filesystem::path path = definitionPathFor(entry);
        const std::string source = readTextFile(path);
        if (!source.empty()) {
            const json root = json::parse(source, nullptr, false, true);
            if (!root.is_discarded() && root.is_object()) {
                applyDefinitionJson(root, path.parent_path(), definition);
            }
        }

        if (definition.id.empty()) {
            definition.id = entry.stableId;
        }
        if (definition.displayName.empty()) {
            definition.displayName = titleCaseFromId(definition.id);
        }
        if (!definition.behavior) {
            finalizeBehaviorDefinition(definition);
        }

        definitions.push_back(std::move(definition));
    }

    return definitions;
}

} // namespace

const ItemRegistry& ItemRegistry::instance() {
    return mutableInstance();
}

ItemRegistry& ItemRegistry::mutableInstance() {
    static ItemRegistry registry;
    return registry;
}

ItemRegistry::ItemRegistry() {
    reload();
}

const ItemDefinition& ItemRegistry::definition(ItemId itemId) const {
    const std::size_t index = static_cast<std::size_t>(itemId);
    if (index < definitions_.size()) {
        return definitions_[index];
    }

    return definitions_.front();
}

const std::vector<ItemDefinition>& ItemRegistry::definitions() const {
    return definitions_;
}

bool ItemRegistry::tryParseId(const std::string& rawValue, ItemId& outItemId) const {
    return ItemCatalog::tryResolve(normalizeItemId(rawValue), outItemId);
}

void ItemRegistry::reload() {
    definitions_ = loadDefinitions();
    if (definitions_.empty()) {
        const ItemCatalogEntry fallbackEntry{ 0, "none", "none" };
        definitions_.push_back(makeFallbackDefinition(fallbackEntry));
    }
}
