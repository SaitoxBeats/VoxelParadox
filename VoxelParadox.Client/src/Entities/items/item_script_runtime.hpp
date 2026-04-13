// File: VoxelParadox.Client/src/Entities/items/item_script_runtime.hpp
// Purpose: executes Lua-backed item scripts for on_use, on_update, and on_pickup hooks.
// Flow: LuaScriptItemBehavior delegates on_use here. Player manages the persistent
//       ItemScriptSession for on_update (per-frame) and on_pickup (on selection).

#pragma once

// 1. Standard Library
#include <string>

// 2. Local Project Modules
#include "items/item_registry.hpp"
#include "items/item_use_context.hpp"

// Forward-declare the opaque Lua state to avoid including lua.h in this header.
struct lua_State;

// ---------------------------------------------------------------------------
// ItemScriptSession
// A persistent Lua runtime bound to the item currently held in the hotbar.
// The session is opened when the player selects a scripted item (triggering
// on_pickup) and closed when they switch to a different item.
// All three hooks — on_pickup, on_update, on_use — share the same lua_State
// so that Lua globals set in one hook are visible in the others.
// ---------------------------------------------------------------------------

struct ItemScriptSession {
    lua_State* luaState = nullptr;
    std::string itemId;
    bool hasOnUse    = false;
    bool hasOnUpdate = false;
    bool hasOnPickup = false;

    ItemScriptSession() = default;
    ItemScriptSession(const ItemScriptSession&) = delete;
    ItemScriptSession& operator=(const ItemScriptSession&) = delete;
    ItemScriptSession(ItemScriptSession&& other) noexcept;
    ItemScriptSession& operator=(ItemScriptSession&& other) noexcept;
    ~ItemScriptSession();

    bool isValid() const { return luaState != nullptr; }

    // Closes and nulls the Lua state. Safe to call multiple times.
    void close();

    // Calls on_use(context). Returns true if the script consumed the use.
    // Errors are logged and fail safely.
    bool callOnUse(ItemUseContext& context, std::string* outError = nullptr);

    // Calls on_update(context, dt). Returns true if the hook ran.
    // Errors are logged and fail safely — the session remains usable.
    bool callOnUpdate(ItemUseContext& context, float dt);

    // Calls on_pickup(context). Returns true if the hook ran.
    // Errors are logged and fail safely.
    bool callOnPickup(ItemUseContext& context);
};

// ---------------------------------------------------------------------------
// openItemScriptSession
// Creates a new session for the given item definition:
//   1. Allocates a Lua state and opens safe standard libraries.
//   2. Loads and boots the script (runs top-level code).
//   3. Detects which hooks (on_use / on_update / on_pickup) are defined.
// Returns an invalid session (isValid() == false) on any failure.
// ---------------------------------------------------------------------------
ItemScriptSession openItemScriptSession(const ItemDefinition& definition);

// ---------------------------------------------------------------------------
// runItemScriptOnUse (stateless fallback)
// Creates a disposable Lua state, calls on_use(context), and closes it.
// Used when no active session exists (e.g. the behavior ran before the player
// had a chance to select the item and open a session).
// ---------------------------------------------------------------------------
bool runItemScriptOnUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError = nullptr
);
