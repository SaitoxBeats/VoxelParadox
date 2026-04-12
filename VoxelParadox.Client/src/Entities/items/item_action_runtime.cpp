// File: VoxelParadox.Client/src/Entities/items/item_action_runtime.cpp
// Purpose: runs declarative item use rules loaded from item.json.
// Flow: validates use conditions first, then executes actions, and finally applies consume/cooldown rules.

// 1. Standard Library
#include <cstdio>
#include <string>

// 2. Local Project Modules
#include "items/item_action_runtime.hpp"

#include "audio/game_audio_controller.hpp"
#include "player/player.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "world/generation/fractal_world.hpp"

namespace {

// --- 1. Condition Helpers ---

bool evaluateCondition(
    const ItemUseConditionDefinition& condition,
    const ItemUseContext& context
) {
    bool result = false;

    switch (condition.type) {
    case ItemUseConditionType::PlayerAlive:
        result = context.player && context.player->isAlive();
        break;

    case ItemUseConditionType::HasTarget:
        result = context.hasTarget;
        break;

    case ItemUseConditionType::NoTarget:
        result = !context.hasTarget;
        break;

    case ItemUseConditionType::PortalTrackerAvailable:
        result = context.portalTracker != nullptr;
        break;

    case ItemUseConditionType::SelectedCountAtLeast:
        result = context.selectedCount >= condition.amount;
        break;

    case ItemUseConditionType::None:
    default:
        result = true;
        break;
    }

    return condition.negate ? !result : result;
}

bool evaluateAllConditions(
    const ItemDefinition& definition,
    const ItemUseContext& context
) {
    for (const ItemUseConditionDefinition& condition : definition.onUse.conditions) {
        if (!evaluateCondition(condition, context)) {
            return false;
        }
    }

    return true;
}

// --- 2. Preflight Helpers ---

int totalSelectedConsumptionCount(const ItemDefinition& definition) {
    int totalConsumeCount = definition.onUse.consumeCount;

    for (const ItemUseActionDefinition& action : definition.onUse.actions) {
        if (action.type != ItemUseActionType::ConsumeItem) {
            continue;
        }

        totalConsumeCount += action.amount;
    }

    return totalConsumeCount;
}

bool preflightUse(
    const ItemDefinition& definition,
    const ItemUseContext& context,
    std::string* outError
) {
    if (context.player == nullptr) {
        if (outError) {
            *outError = "Missing player context.";
        }
        return false;
    }

    if (!evaluateAllConditions(definition, context)) {
        if (outError) {
            *outError = "Use conditions were not satisfied.";
        }
        return false;
    }

    if (definition.onUse.cooldownSeconds > 0.0f) {
        const float remainingCooldownSeconds =
            context.player->getItemUseCooldownRemainingSeconds(context.selectedItem.itemId);
        if (remainingCooldownSeconds > 0.0f) {
            if (outError) {
                *outError = "Item is still on cooldown.";
            }
            return false;
        }
    }

    const int selectedConsumeCount = totalSelectedConsumptionCount(definition);
    if (selectedConsumeCount > 0 && context.selectedCount < selectedConsumeCount) {
        if (outError) {
            *outError = "Not enough selected items to consume.";
        }
        return false;
    }

    for (const ItemUseActionDefinition& action : definition.onUse.actions) {
        if (action.type != ItemUseActionType::GiveItem) {
            continue;
        }

        if (action.amount <= 0 || action.item.empty()) {
            if (outError) {
                *outError = "Invalid give_item action.";
            }
            return false;
        }

        if (!context.player->canAcceptInventoryItem(action.item, action.amount)) {
            if (outError) {
                *outError = "Inventory cannot accept the granted item.";
            }
            return false;
        }
    }

    return true;
}

// --- 3. Action Execution ---

bool executeAction(
    const ItemUseActionDefinition& action,
    ItemUseContext& context,
    std::string* outError
) {
    switch (action.type) {
    case ItemUseActionType::PrintTerminal:
        std::printf("%s\n", action.stringValue.c_str());
        return true;

    case ItemUseActionType::OpenPortalTracker:
        if (context.portalTracker == nullptr) {
            if (outError) {
                *outError = "Portal tracker HUD is unavailable.";
            }
            return false;
        }

        if (!context.portalTracker->isMenuOpen()) {
            context.portalTracker->toggleMenu();
        }
        return true;

    case ItemUseActionType::PlaySound:
        if (context.audioController == nullptr) {
            if (outError) {
                *outError = "Audio controller is unavailable.";
            }
            return false;
        }

        context.audioController->playItemEvent(action.stringValue);
        return true;

    case ItemUseActionType::HealPlayer:
        if (context.player == nullptr) {
            if (outError) {
                *outError = "Missing player context.";
            }
            return false;
        }

        context.player->setLifePoints(
            context.player->getLifePoints() + action.amount
        );
        return true;

    case ItemUseActionType::GiveItem:
        if (context.player == nullptr) {
            if (outError) {
                *outError = "Missing player context.";
            }
            return false;
        }

        return context.player->tryAddItemToInventory(action.item, action.amount);

    case ItemUseActionType::ConsumeItem:
        if (context.player == nullptr) {
            if (outError) {
                *outError = "Missing player context.";
            }
            return false;
        }

        return context.player->tryConsumeSelectedInventoryItem(action.amount);

    case ItemUseActionType::None:
    default:
        return true;
    }
}

} // namespace

bool beginDeclarativeItemUse(
    const ItemDefinition& definition,
    const ItemUseContext& context,
    std::string* outError
) {
    return preflightUse(definition, context, outError);
}

bool executeDeclarativeItemActions(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError
) {
    for (const ItemUseActionDefinition& action : definition.onUse.actions) {
        if (!executeAction(action, context, outError)) {
            return false;
        }
    }

    return true;
}

bool finalizeDeclarativeItemUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError
) {
    if (definition.onUse.consumeCount > 0) {
        if (!context.player->tryConsumeSelectedInventoryItem(definition.onUse.consumeCount)) {
            if (outError) {
                *outError = "Failed to consume the selected item.";
            }
            return false;
        }
    }

    if (definition.onUse.cooldownSeconds > 0.0f) {
        context.player->startItemUseCooldown(
            context.selectedItem.itemId,
            definition.onUse.cooldownSeconds
        );
    }

    return true;
}

bool runDeclarativeItemUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError
) {
    // --- 1. Validate Use ---
    if (!beginDeclarativeItemUse(definition, context, outError)) {
        return false;
    }

    // --- 2. Execute Actions ---
    if (!executeDeclarativeItemActions(definition, context, outError)) {
        return false;
    }

    // --- 3. Finalize Consume & Cooldown ---
    return finalizeDeclarativeItemUse(definition, context, outError);
}
