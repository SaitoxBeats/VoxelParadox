// File: VoxelParadox.Client/src/Entities/items/item_use_context.hpp
// Purpose: carries the controlled runtime bridge exposed to item use behaviors.
// Flow: declarative actions and Lua scripts read and mutate gameplay only through this context.

#pragma once

// 1. Local Project Modules
#include "gameplay/gameplay_script_context.hpp"
#include "items/item_types.hpp"
#include "player/player.hpp"

// Forward declaration — full type is in item_script_runtime.hpp.
// Included by item_behavior.cpp and player_item_script.cpp which need the full type.
struct ItemScriptSession;

struct ItemUseContext : Gameplay::ScriptContext {
    InventoryItem selectedItem{};
    int selectedCount = 0;

    // Optional: pointer to the active persistent Lua session for the selected item.
    // Set by Player::updateItemScript and passed to item behaviors so all three
    // hooks (on_pickup, on_update, on_use) share the same lua_State and globals.
    // Null when no session exists (e.g. during stateless fallback calls).
    ItemScriptSession* scriptSession = nullptr;

    void inheritFrom(
        const Gameplay::Context& gameplayContext,
        FractalWorld* currentWorld
    ) {
        Gameplay::ScriptContext::inheritFrom(gameplayContext, currentWorld);
        inheritTargetFromPlayer(currentWorld);
        inheritSelectedItemFromPlayer();
    }

    void inheritSelectedItemFromPlayer() {
        if (!player) {
            selectedItem = InventoryItem{};
            selectedCount = 0;
            return;
        }

        selectedItem = player->getSelectedHotbarItem();
        selectedCount = player->getSelectedHotbarCount();
    }
};
