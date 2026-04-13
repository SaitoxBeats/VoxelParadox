// File: VoxelParadox.Client/src/Entities/items/item_behavior.cpp
// Purpose: implements the stateless built-in item behavior families.
// Flow: the item registry resolves string behavior ids to one of these reusable objects.

// 1. Standard Library
#include <algorithm>
#include <string>

// 2. Local Project Modules
#include "items/item_behavior.hpp"

#include "items/item_action_runtime.hpp"
#include "items/item_script_runtime.hpp"
#include "items/item_use_context.hpp"
#include "items/item_registry.hpp"

bool ItemBehavior::onUse(const ItemDefinition& definition, ItemUseContext& context) const {
    (void)definition;
    (void)context;
    return false;
}

bool ItemBehavior::isTool(const ItemDefinition& definition) const {
    (void)definition;
    return false;
}

ToolDefinition ItemBehavior::getToolDefinition(const ItemDefinition& definition) const {
    (void)definition;
    return {};
}

int ItemBehavior::getStackLimit(const ItemDefinition& definition) const {
    return definition.stackLimit;
}

bool DefaultItemBehavior::isTool(const ItemDefinition& definition) const {
    (void)definition;
    return false;
}

ToolDefinition DefaultItemBehavior::getToolDefinition(const ItemDefinition& definition) const {
    (void)definition;
    return {};
}

int DefaultItemBehavior::getStackLimit(const ItemDefinition& definition) const {
    return std::max(definition.stackLimit, 1);
}

bool ToolItemBehavior::isTool(const ItemDefinition& definition) const {
    return definition.hasToolDefinition;
}

ToolDefinition ToolItemBehavior::getToolDefinition(const ItemDefinition& definition) const {
    return definition.hasToolDefinition ? definition.tool : ToolDefinition{};
}

int ToolItemBehavior::getStackLimit(const ItemDefinition& definition) const {
    return std::max(definition.stackLimit, 1);
}

const ItemBehavior& resolveItemBehavior(std::string_view behaviorId) {
    static const DefaultItemBehavior kDefaultBehavior{};
    static const ToolItemBehavior kToolBehavior{};
    static const DeclarativeItemBehavior kDeclarativeBehavior{};
    static const LuaScriptItemBehavior kLuaScriptBehavior{};

    if (behaviorId == "tool") {
        return kToolBehavior;
    }

    if (behaviorId == "lua") {
        return kLuaScriptBehavior;
    }

    if (behaviorId == "declarative" || behaviorId == "portal_tracker") {
        return kDeclarativeBehavior;
    }

    return kDefaultBehavior;
}

bool DeclarativeItemBehavior::onUse(const ItemDefinition& definition, ItemUseContext& context) const {
    return runDeclarativeItemUse(definition, context);
}

bool LuaScriptItemBehavior::onUse(const ItemDefinition& definition, ItemUseContext& context) const {
    const bool hasDeclarativeUse = !definition.onUse.empty();

    if (hasDeclarativeUse && !beginDeclarativeItemUse(definition, context)) {
        return false;
    }

    if (hasDeclarativeUse && !executeDeclarativeItemActions(definition, context)) {
        return false;
    }

    // Prefer the active persistent session so on_use shares globals with on_update/on_pickup.
    // Fall back to a stateless one-shot call when no session is available.
    bool scriptConsumed;
    if (context.scriptSession && context.scriptSession->isValid() &&
        context.scriptSession->hasOnUse) {
        scriptConsumed = context.scriptSession->callOnUse(context);
    } else {
        scriptConsumed = runItemScriptOnUse(definition, context);
    }

    if (hasDeclarativeUse && !finalizeDeclarativeItemUse(definition, context)) {
        return false;
    }

    return hasDeclarativeUse || scriptConsumed;
}
