// File: VoxelParadox.Client/src/Entities/player/player_item_script.cpp
// Purpose: manages the lifecycle of the active ItemScriptSession and dispatches
//          on_pickup and on_update hooks each frame.
// Flow: Gameplay::RuntimeSystem calls Player::updateItemScript once per frame after
//       player.update(). When the selected item changes the old session is destroyed,
//       a new one is opened, and on_pickup is called. Then on_update is called every
//       frame as long as the same scripted item remains selected.
//
// This file also defines Player's special members out-of-line so the compiler can see the
// complete ItemScriptSession type when instantiating ~unique_ptr<ItemScriptSession>.

// 1. Standard Library
#include <memory>
#include <string>

// 2. Local Project Modules
#include "player/player.hpp"

#include "gameplay/gameplay_context.hpp"
#include "items/item_catalog.hpp"
#include "items/item_registry.hpp"
#include "items/item_script_runtime.hpp"
#include "items/item_use_context.hpp"
#include "world/generation/fractal_world.hpp"

// ---------------------------------------------------------------------------
// Player::~Player
// Defined here (not in the header) so ~unique_ptr<ItemScriptSession> is
// compiled in a translation unit where ItemScriptSession is fully defined.
// ---------------------------------------------------------------------------

Player::Player(Player&& other) noexcept = default;

Player& Player::operator=(Player&& other) noexcept = default;

Player::~Player() = default;

// ---------------------------------------------------------------------------
// Player::getActiveItemScriptSession
// ---------------------------------------------------------------------------

ItemScriptSession* Player::getActiveItemScriptSession() const {
    return activeScriptSession_.get();
}

// ---------------------------------------------------------------------------
// Player::updateItemScript
// Called once per frame by Gameplay::RuntimeSystem after player.update().
// ---------------------------------------------------------------------------

void Player::updateItemScript(
    Gameplay::Context& gameplayContext,
    FractalWorld* world,
    float dt
) {
    // --- 1. Identify the currently selected item ---
    const InventoryItem& selectedItem = hotbar.getSelectedItem();
    const int selectedIndex = hotbar.getSelectedIndex();

    // --- 2. Detect item change ---
    const bool itemChanged =
        selectedIndex != activeScriptHotbarIndex_ ||
        selectedItem  != activeScriptItem_;

    if (itemChanged) {
        // --- 3. Close the old session (if any) ---
        activeScriptSession_.reset();
        activeScriptItem_         = selectedItem;
        activeScriptHotbarIndex_  = selectedIndex;

        // --- 4. Open a new session if the selected item has a Lua script ---
        if (selectedItem.isItem()) {
            const ItemDefinition& definition = getItemDefinition(selectedItem.itemId);

            if (definition.script.enabled && !definition.script.source.empty()) {
                ItemScriptSession newSession = openItemScriptSession(definition);

                if (newSession.isValid()) {
                    activeScriptSession_ =
                        std::make_unique<ItemScriptSession>(std::move(newSession));

                    // --- 5. Dispatch on_pickup ---
                    if (activeScriptSession_->hasOnPickup) {
                        ItemUseContext pickupContext{};
                        pickupContext.inheritFrom(gameplayContext, world);
                        pickupContext.scriptSession = activeScriptSession_.get();
                        activeScriptSession_->callOnPickup(pickupContext);
                    }
                }
            }
        }
    }

    // --- 6. Dispatch on_update ---
    if (activeScriptSession_ && activeScriptSession_->hasOnUpdate) {
        ItemUseContext updateContext{};
        updateContext.inheritFrom(gameplayContext, world);
        updateContext.scriptSession = activeScriptSession_.get();
        activeScriptSession_->callOnUpdate(updateContext, dt);
    }
}
