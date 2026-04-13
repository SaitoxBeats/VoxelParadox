// File: VoxelParadox.Client/src/Entities/items/item_script_runtime.cpp
// Purpose: provides the persistent session-based and stateless Lua runtimes for item scripts.
// Flow: ItemScriptSession is opened when the player selects a scripted item; all three hooks
//       (on_pickup, on_update, on_use) share the same lua_State for consistent global state.
//       runItemScriptOnUse is a stateless fallback that creates and destroys a lua_State per call.

// 1. Standard Library
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// 2. Third-party Libraries
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// 3. Local Project Modules
#include "items/item_script_runtime.hpp"

#include "audio/game_audio_controller.hpp"
#include "items/item_catalog.hpp"
#include "runtime/state/game_chat.hpp"
#include "player/player.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "world/generation/fractal_world.hpp"

namespace {

// ---------------------------------------------------------------------------
// 1. Lua Utilities
// ---------------------------------------------------------------------------

ItemUseContext* getLuaContext(lua_State* L) {
    return static_cast<ItemUseContext*>(lua_touserdata(L, lua_upvalueindex(1)));
}

bool readLuaNotificationColor(lua_State* L, int index, glm::vec3& outColor) {
    const int absIndex = lua_absindex(L, index);
    if (!lua_istable(L, absIndex)) {
        return false;
    }

    bool hasComponent = false;
    auto readComponent = [&](const char* fieldName, float& component) {
        lua_getfield(L, absIndex, fieldName);
        if (lua_isnumber(L, -1)) {
            component = static_cast<float>(lua_tonumber(L, -1));
            hasComponent = true;
        }
        lua_pop(L, 1);
    };

    readComponent("r", outColor.r);
    readComponent("g", outColor.g);
    readComponent("b", outColor.b);

    if (hasComponent) {
        return true;
    }

    for (int channel = 0; channel < 3; ++channel) {
        lua_rawgeti(L, absIndex, channel + 1);
        if (lua_isnumber(L, -1)) {
            outColor[channel] = static_cast<float>(lua_tonumber(L, -1));
            hasComponent = true;
        }
        lua_pop(L, 1);
    }

    return hasComponent;
}

bool readLuaNotificationSegment(lua_State* L, int index, GameChatTextSegment& outSegment) {
    const int absIndex = lua_absindex(L, index);

    if (lua_isstring(L, absIndex)) {
        outSegment.text = lua_tostring(L, absIndex);
        outSegment.color = GameChatTheme::kDefaultHistoryColor;
        return !outSegment.text.empty();
    }

    if (!lua_istable(L, absIndex)) {
        return false;
    }

    lua_getfield(L, absIndex, "text");
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    outSegment.text = lua_tostring(L, -1);
    lua_pop(L, 1);
    outSegment.color = GameChatTheme::kDefaultHistoryColor;

    lua_getfield(L, absIndex, "color");
    if (lua_istable(L, -1)) {
        glm::vec3 color = outSegment.color;
        if (readLuaNotificationColor(L, -1, color)) {
            outSegment.color = color;
        }
    }
    lua_pop(L, 1);

    return !outSegment.text.empty();
}

std::vector<GameChatTextSegment> readLuaNotificationSegments(lua_State* L, int index) {
    std::vector<GameChatTextSegment> segments;
    const int absIndex = lua_absindex(L, index);
    const std::size_t segmentCount = lua_rawlen(L, absIndex);

    for (std::size_t i = 1; i <= segmentCount; ++i) {
        lua_rawgeti(L, absIndex, static_cast<lua_Integer>(i));

        GameChatTextSegment segment{};
        if (readLuaNotificationSegment(L, -1, segment)) {
            segments.push_back(std::move(segment));
        }

        lua_pop(L, 1);
    }

    return segments;
}

// ---------------------------------------------------------------------------
// 2. Movement State Helper
// ---------------------------------------------------------------------------

const char* movementStateToString(PlayerMovementState state) {
    switch (state) {
        case PlayerMovementState::Idle:      return "idle";
        case PlayerMovementState::Walking:   return "walking";
        case PlayerMovementState::Running:   return "running";
        case PlayerMovementState::Crouching: return "crouching";
        case PlayerMovementState::Jumping:   return "jumping";
        case PlayerMovementState::Falling:   return "falling";
        default:                             return "idle";
    }
}

// ---------------------------------------------------------------------------
// 3. Lua Context Functions — Logging & Chat
// ---------------------------------------------------------------------------

int luaContextLog(lua_State* L) {
    std::printf("%s\n", luaL_checkstring(L, 1));
    return 0;
}

int luaContextPushNotification(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    if (!ctx || !ctx->gameChat) {
        return 0;
    }

    const int valueType = lua_type(L, 1);
    if (valueType == LUA_TSTRING) {
        ctx->gameChat->pushNotification(std::string(lua_tostring(L, 1)));
        return 0;
    }

    if (valueType != LUA_TTABLE) {
        return luaL_error(L, "push_notification expects a string or a table of segments.");
    }

    GameChatTextSegment singleSegment{};
    if (readLuaNotificationSegment(L, 1, singleSegment)) {
        ctx->gameChat->pushNotification(
            std::vector<GameChatTextSegment>{ std::move(singleSegment) });
        return 0;
    }

    const std::vector<GameChatTextSegment> segments = readLuaNotificationSegments(L, 1);
    if (!segments.empty()) {
        ctx->gameChat->pushNotification(std::move(segments));
        return 0;
    }

    return luaL_error(L, "push_notification expects a string or a table of segments.");
}

// ---------------------------------------------------------------------------
// 4. Lua Context Functions — Player Health
// ---------------------------------------------------------------------------

int luaContextGetPlayerLife(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L, (ctx && ctx->player) ? ctx->player->getLifePoints() : 0);
    return 1;
}

int luaContextGetPlayerMaxLife(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L, (ctx && ctx->player) ? ctx->player->getMaxLifePoints() : 0);
    return 1;
}

int luaContextIsPlayerAlive(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->player && ctx->player->isAlive()) ? 1 : 0);
    return 1;
}

int luaContextHealPlayer(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const int amount = static_cast<int>(luaL_checkinteger(L, 1));
    if (ctx && ctx->player) {
        ctx->setPlayerLifePoints(ctx->player->getLifePoints() + amount);
    }
    return 0;
}

int luaContextDamagePlayer(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const int amount = static_cast<int>(luaL_checkinteger(L, 1));
    if (ctx && ctx->player && amount > 0) {
        ctx->player->applyDamage(amount);
    }
    return 0;
}

int luaContextSetPlayerHealth(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const int amount = static_cast<int>(luaL_checkinteger(L, 1));
    if (ctx) {
        ctx->setPlayerLifePoints(amount);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Lua Context Functions — Player Experience
// ---------------------------------------------------------------------------

int luaContextGetPlayerXp(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushnumber(L, static_cast<lua_Number>(
        (ctx && ctx->player) ? ctx->player->getExperiencePoints() : 0.0f));
    return 1;
}

int luaContextGetPlayerXpMax(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushnumber(L, static_cast<lua_Number>(
        (ctx && ctx->player) ? ctx->player->getMaxExperiencePoints() : 0.0f));
    return 1;
}

int luaContextGetPlayerLevel(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L, (ctx && ctx->player) ? ctx->player->getExperienceLevel() : 0);
    return 1;
}

int luaContextSetPlayerXp(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const float xp = static_cast<float>(luaL_checknumber(L, 1));
    if (ctx && ctx->player) {
        ctx->setPlayerExperience(xp);
    }
    return 0;
}

int luaContextAddPlayerXp(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const float xp = static_cast<float>(luaL_checknumber(L, 1));
    if (ctx && ctx->player) {
        ctx->addPlayerExperience(xp);
    }
    return 0;
}

int luaContextSetPlayerLevel(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const int level = static_cast<int>(luaL_checkinteger(L, 1));
    if (ctx && ctx->player) {
        ctx->player->setExperienceLevel(level, ctx->eventQueue);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 6. Lua Context Functions — Player Physics & Movement
// ---------------------------------------------------------------------------

int luaContextGetPlayerPosition(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::vec3 pos = (ctx && ctx->player) ? ctx->player->camera.position : glm::vec3(0.0f);
    lua_pushnumber(L, static_cast<lua_Number>(pos.x));
    lua_pushnumber(L, static_cast<lua_Number>(pos.y));
    lua_pushnumber(L, static_cast<lua_Number>(pos.z));
    return 3;
}

int luaContextGetPlayerVelocity(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::vec3 vel = (ctx && ctx->player) ? ctx->player->velocity : glm::vec3(0.0f);
    lua_pushnumber(L, static_cast<lua_Number>(vel.x));
    lua_pushnumber(L, static_cast<lua_Number>(vel.y));
    lua_pushnumber(L, static_cast<lua_Number>(vel.z));
    return 3;
}

int luaContextGetPlayerLook(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::vec3 fwd = (ctx && ctx->player)
        ? ctx->player->camera.getForward()
        : glm::vec3(0.0f, 0.0f, -1.0f);
    lua_pushnumber(L, static_cast<lua_Number>(fwd.x));
    lua_pushnumber(L, static_cast<lua_Number>(fwd.y));
    lua_pushnumber(L, static_cast<lua_Number>(fwd.z));
    return 3;
}

int luaContextIsPlayerGrounded(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->player && ctx->player->isGrounded()) ? 1 : 0);
    return 1;
}

int luaContextIsPlayerCrouching(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->player && ctx->player->isCrouching()) ? 1 : 0);
    return 1;
}

int luaContextGetMovementState(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const PlayerMovementState state = (ctx && ctx->player)
        ? ctx->player->getMovementState()
        : PlayerMovementState::Idle;
    lua_pushstring(L, movementStateToString(state));
    return 1;
}

// ---------------------------------------------------------------------------
// 7. Lua Context Functions — Player State Checks
// ---------------------------------------------------------------------------

int luaContextIsSandboxMode(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L,
        (ctx && ctx->player && ctx->player->isSandboxModeEnabled()) ? 1 : 0);
    return 1;
}

int luaContextPlayerHasSpawnpoint(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->player && ctx->player->hasSpawnpoint()) ? 1 : 0);
    return 1;
}

int luaContextGetSelectedHotbarIndex(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L,
        (ctx && ctx->player) ? ctx->player->getSelectedHotbarIndex() : 0);
    return 1;
}

int luaContextIsBreakingBlock(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L,
        (ctx && ctx->player && ctx->player->isBreakingTargetBlock()) ? 1 : 0);
    return 1;
}

int luaContextGetBreakingProgress(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushnumber(L, static_cast<lua_Number>(
        (ctx && ctx->player) ? ctx->player->getBreakingProgress() : 0.0f));
    return 1;
}

// ---------------------------------------------------------------------------
// 8. Lua Context Functions — Inventory
// ---------------------------------------------------------------------------

int luaContextAddItem(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const char* rawItemId = luaL_checkstring(L, 1);
    const int amount = static_cast<int>(luaL_optinteger(L, 2, 1));

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool added = parsed && ctx && ctx->tryAddInventoryItem(item, amount);

    lua_pushboolean(L, added ? 1 : 0);
    return 1;
}

int luaContextRemoveItem(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const char* rawItemId = luaL_checkstring(L, 1);
    const int amount = static_cast<int>(luaL_optinteger(L, 2, 1));

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool removed = parsed && ctx && ctx->tryRemoveInventoryItem(item, amount);

    lua_pushboolean(L, removed ? 1 : 0);
    return 1;
}

int luaContextHasItem(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const char* rawItemId = luaL_checkstring(L, 1);

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool hasIt = parsed && ctx && ctx->countInventoryItem(item) > 0;

    lua_pushboolean(L, hasIt ? 1 : 0);
    return 1;
}

int luaContextCountItem(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const char* rawItemId = luaL_checkstring(L, 1);

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const int count = (parsed && ctx) ? ctx->countInventoryItem(item) : 0;

    lua_pushinteger(L, count);
    return 1;
}

int luaContextConsumeSelected(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const int amount = static_cast<int>(luaL_optinteger(L, 1, 1));
    lua_pushboolean(L,
        (ctx && ctx->tryConsumeSelectedInventoryItem(amount)) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// 9. Lua Context Functions — Portals & Audio
// ---------------------------------------------------------------------------

int luaContextOpenPortalTracker(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    if (ctx && ctx->portalTracker && !ctx->portalTracker->isMenuOpen()) {
        ctx->portalTracker->toggleMenu();
    }
    return 0;
}

int luaContextCreatePortalForTargetBlock(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->tryCreatePortalForTargetBlock()) ? 1 : 0);
    return 1;
}

int luaContextPlayAudioEvent(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const char* eventName = luaL_checkstring(L, 1);
    if (ctx && ctx->audioController) {
        ctx->audioController->playItemEvent(eventName);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 10. Lua Context Functions — Target & World
// ---------------------------------------------------------------------------

int luaContextHasTarget(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushboolean(L, (ctx && ctx->hasTarget) ? 1 : 0);
    return 1;
}

int luaContextGetTargetBlockId(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushstring(L,
        (ctx && ctx->hasTarget) ? getBlockId(ctx->targetBlockType) : "air");
    return 1;
}

int luaContextGetTargetBlockPosition(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::ivec3 block = ctx ? ctx->targetBlock : glm::ivec3(0);
    lua_pushinteger(L, block.x);
    lua_pushinteger(L, block.y);
    lua_pushinteger(L, block.z);
    return 3;
}

int luaContextGetTargetNormal(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::ivec3 normal = ctx ? ctx->targetNormal : glm::ivec3(0);
    lua_pushinteger(L, normal.x);
    lua_pushinteger(L, normal.y);
    lua_pushinteger(L, normal.z);
    return 3;
}

int luaContextGetPlacePosition(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    const glm::ivec3 pos = (ctx && ctx->hasTarget)
        ? ctx->targetBlock + ctx->targetNormal
        : glm::ivec3(0);
    lua_pushinteger(L, pos.x);
    lua_pushinteger(L, pos.y);
    lua_pushinteger(L, pos.z);
    return 3;
}

int luaContextGetCurrentWorldSeed(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L,
        (ctx && ctx->world)
            ? static_cast<lua_Integer>(ctx->world->seed)
            : 0);
    return 1;
}

int luaContextGetSelectedItemId(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushstring(L, ctx ? getInventoryItemId(ctx->selectedItem) : "none");
    return 1;
}

int luaContextGetSelectedCount(lua_State* L) {
    ItemUseContext* ctx = getLuaContext(L);
    lua_pushinteger(L, ctx ? ctx->selectedCount : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// 11. Context Table Builder
// ---------------------------------------------------------------------------

void pushContextFunction(lua_State* L, ItemUseContext* ctx,
                         const char* name, lua_CFunction fn) {
    lua_pushstring(L, name);
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, fn, 1);
    lua_settable(L, -3);
}

void pushContextSnapshot(lua_State* L, const ItemUseContext& ctx) {
    // Selected item
    lua_pushstring(L, "selected_item_id");
    lua_pushstring(L, getInventoryItemId(ctx.selectedItem));
    lua_settable(L, -3);

    lua_pushstring(L, "selected_count");
    lua_pushinteger(L, ctx.selectedCount);
    lua_settable(L, -3);

    lua_pushstring(L, "selected_hotbar_index");
    lua_pushinteger(L, ctx.player ? ctx.player->getSelectedHotbarIndex() : 0);
    lua_settable(L, -3);

    // Target
    lua_pushstring(L, "has_target");
    lua_pushboolean(L, ctx.hasTarget ? 1 : 0);
    lua_settable(L, -3);

    lua_pushstring(L, "target_block_id");
    lua_pushstring(L, getBlockId(ctx.targetBlockType));
    lua_settable(L, -3);

    // World
    lua_pushstring(L, "current_world_seed");
    lua_pushinteger(L,
        ctx.world ? static_cast<lua_Integer>(ctx.world->seed) : 0);
    lua_settable(L, -3);

    // Player flags
    lua_pushstring(L, "has_opened_first_portal");
    lua_pushboolean(L,
        (ctx.player && ctx.player->hasOpenedFirstPortal()) ? 1 : 0);
    lua_settable(L, -3);

    lua_pushstring(L, "is_grounded");
    lua_pushboolean(L, (ctx.player && ctx.player->isGrounded()) ? 1 : 0);
    lua_settable(L, -3);

    lua_pushstring(L, "is_crouching");
    lua_pushboolean(L, (ctx.player && ctx.player->isCrouching()) ? 1 : 0);
    lua_settable(L, -3);

    lua_pushstring(L, "movement_state");
    lua_pushstring(L,
        ctx.player ? movementStateToString(ctx.player->getMovementState()) : "idle");
    lua_settable(L, -3);

    lua_pushstring(L, "is_sandbox_mode");
    lua_pushboolean(L,
        (ctx.player && ctx.player->isSandboxModeEnabled()) ? 1 : 0);
    lua_settable(L, -3);

    lua_pushstring(L, "player_xp_max");
    lua_pushnumber(L,
        ctx.player
            ? static_cast<lua_Number>(ctx.player->getMaxExperiencePoints())
            : 0.0);
    lua_settable(L, -3);
}

void buildContextTable(lua_State* L, ItemUseContext& ctx) {
    lua_newtable(L);

    pushContextSnapshot(L, ctx);

    // Logging & Chat
    pushContextFunction(L, &ctx, "log",               luaContextLog);
    pushContextFunction(L, &ctx, "push_notification", luaContextPushNotification);

    // Player Health
    pushContextFunction(L, &ctx, "get_player_life",     luaContextGetPlayerLife);
    pushContextFunction(L, &ctx, "get_player_max_life", luaContextGetPlayerMaxLife);
    pushContextFunction(L, &ctx, "is_player_alive",     luaContextIsPlayerAlive);
    pushContextFunction(L, &ctx, "heal_player",         luaContextHealPlayer);
    pushContextFunction(L, &ctx, "damage_player",       luaContextDamagePlayer);
    pushContextFunction(L, &ctx, "set_player_health",   luaContextSetPlayerHealth);

    // Player Experience
    pushContextFunction(L, &ctx, "get_player_xp",     luaContextGetPlayerXp);
    pushContextFunction(L, &ctx, "get_player_xp_max", luaContextGetPlayerXpMax);
    pushContextFunction(L, &ctx, "get_player_level",  luaContextGetPlayerLevel);
    pushContextFunction(L, &ctx, "set_player_xp",     luaContextSetPlayerXp);
    pushContextFunction(L, &ctx, "add_player_xp",     luaContextAddPlayerXp);
    pushContextFunction(L, &ctx, "set_player_level",  luaContextSetPlayerLevel);

    // Player Physics & Movement
    pushContextFunction(L, &ctx, "get_player_position",  luaContextGetPlayerPosition);
    pushContextFunction(L, &ctx, "get_player_velocity",  luaContextGetPlayerVelocity);
    pushContextFunction(L, &ctx, "get_player_look",      luaContextGetPlayerLook);
    pushContextFunction(L, &ctx, "is_player_grounded",   luaContextIsPlayerGrounded);
    pushContextFunction(L, &ctx, "is_player_crouching",  luaContextIsPlayerCrouching);
    pushContextFunction(L, &ctx, "get_movement_state",   luaContextGetMovementState);

    // Player State Checks
    pushContextFunction(L, &ctx, "is_sandbox_mode",           luaContextIsSandboxMode);
    pushContextFunction(L, &ctx, "player_has_spawnpoint",     luaContextPlayerHasSpawnpoint);
    pushContextFunction(L, &ctx, "get_selected_hotbar_index", luaContextGetSelectedHotbarIndex);
    pushContextFunction(L, &ctx, "is_breaking_block",         luaContextIsBreakingBlock);
    pushContextFunction(L, &ctx, "get_breaking_progress",     luaContextGetBreakingProgress);

    // Inventory
    pushContextFunction(L, &ctx, "add_item",         luaContextAddItem);
    pushContextFunction(L, &ctx, "remove_item",      luaContextRemoveItem);
    pushContextFunction(L, &ctx, "has_item",         luaContextHasItem);
    pushContextFunction(L, &ctx, "count_item",       luaContextCountItem);
    pushContextFunction(L, &ctx, "consume_selected", luaContextConsumeSelected);

    // Portals & Audio
    pushContextFunction(L, &ctx, "open_portal_tracker",
                        luaContextOpenPortalTracker);
    pushContextFunction(L, &ctx, "create_portal_for_target_block",
                        luaContextCreatePortalForTargetBlock);
    pushContextFunction(L, &ctx, "play_audio_event", luaContextPlayAudioEvent);

    // Target & World
    pushContextFunction(L, &ctx, "has_target",                luaContextHasTarget);
    pushContextFunction(L, &ctx, "get_target_block_id",       luaContextGetTargetBlockId);
    pushContextFunction(L, &ctx, "get_target_block_position", luaContextGetTargetBlockPosition);
    pushContextFunction(L, &ctx, "get_target_normal",         luaContextGetTargetNormal);
    pushContextFunction(L, &ctx, "get_place_position",        luaContextGetPlacePosition);
    pushContextFunction(L, &ctx, "get_current_world_seed",    luaContextGetCurrentWorldSeed);
    pushContextFunction(L, &ctx, "get_selected_item_id",      luaContextGetSelectedItemId);
    pushContextFunction(L, &ctx, "get_selected_count",        luaContextGetSelectedCount);
}

void openSafeLibraries(lua_State* L) {
    luaL_requiref(L, "_G",            luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(L, 1);
}

// Loads the script source into an existing Lua state and boots it (runs top-level code).
// Returns true on success. On failure, closes the state and writes an error message.
bool loadAndBootScript(lua_State* L, const ItemDefinition& definition,
                       std::string* outError) {
    const int loadResult = luaL_loadbufferx(
        L,
        definition.script.source.c_str(),
        definition.script.source.size(),
        definition.script.relativePath.c_str(),
        "t"
    );

    if (loadResult != LUA_OK) {
        const std::string err =
            lua_isstring(L, -1) ? lua_tostring(L, -1) : "Unknown load error.";
        if (outError) *outError = err;
        std::printf("[Items][Lua] Failed to load script for '%s': %s\n",
                    definition.id.c_str(), err.c_str());
        return false;
    }

    const int bootResult = lua_pcall(L, 0, 0, 0);
    if (bootResult != LUA_OK) {
        const std::string err =
            lua_isstring(L, -1) ? lua_tostring(L, -1) : "Unknown boot error.";
        if (outError) *outError = err;
        std::printf("[Items][Lua] Failed to boot script for '%s': %s\n",
                    definition.id.c_str(), err.c_str());
        return false;
    }

    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// ItemScriptSession — implementation
// ---------------------------------------------------------------------------

ItemScriptSession::ItemScriptSession(ItemScriptSession&& other) noexcept
    : luaState(other.luaState)
    , itemId(std::move(other.itemId))
    , hasOnUse(other.hasOnUse)
    , hasOnUpdate(other.hasOnUpdate)
    , hasOnPickup(other.hasOnPickup)
{
    other.luaState = nullptr;
}

ItemScriptSession& ItemScriptSession::operator=(ItemScriptSession&& other) noexcept {
    if (this != &other) {
        close();
        luaState    = other.luaState;
        itemId      = std::move(other.itemId);
        hasOnUse    = other.hasOnUse;
        hasOnUpdate = other.hasOnUpdate;
        hasOnPickup = other.hasOnPickup;
        other.luaState = nullptr;
    }
    return *this;
}

ItemScriptSession::~ItemScriptSession() {
    close();
}

void ItemScriptSession::close() {
    if (luaState) {
        lua_close(luaState);
        luaState = nullptr;
    }
}

bool ItemScriptSession::callOnUse(ItemUseContext& context, std::string* outError) {
    if (!luaState || !hasOnUse) {
        return false;
    }

    lua_getglobal(luaState, "on_use");
    if (!lua_isfunction(luaState, -1)) {
        lua_pop(luaState, 1);
        return false;
    }

    buildContextTable(luaState, context);

    const int callResult = lua_pcall(luaState, 1, 1, 0);
    if (callResult != LUA_OK) {
        const std::string err =
            lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "Unknown call error.";
        if (outError) *outError = err;
        std::printf("[Items][Lua] on_use error for '%s': %s\n",
                    itemId.c_str(), err.c_str());
        lua_pop(luaState, 1);
        return false;
    }

    bool consumed = true;
    if (lua_isboolean(luaState, -1)) {
        consumed = lua_toboolean(luaState, -1) != 0;
    }
    lua_pop(luaState, 1);
    return consumed;
}

bool ItemScriptSession::callOnUpdate(ItemUseContext& context, float dt) {
    if (!luaState || !hasOnUpdate) {
        return false;
    }

    lua_getglobal(luaState, "on_update");
    if (!lua_isfunction(luaState, -1)) {
        lua_pop(luaState, 1);
        return false;
    }

    buildContextTable(luaState, context);
    lua_pushnumber(luaState, static_cast<lua_Number>(dt));

    const int callResult = lua_pcall(luaState, 2, 0, 0);
    if (callResult != LUA_OK) {
        std::printf("[Items][Lua] on_update error for '%s': %s\n",
                    itemId.c_str(),
                    lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "?");
        lua_pop(luaState, 1);
        return false;
    }

    return true;
}

bool ItemScriptSession::callOnPickup(ItemUseContext& context) {
    if (!luaState || !hasOnPickup) {
        return false;
    }

    lua_getglobal(luaState, "on_pickup");
    if (!lua_isfunction(luaState, -1)) {
        lua_pop(luaState, 1);
        return false;
    }

    buildContextTable(luaState, context);

    const int callResult = lua_pcall(luaState, 1, 0, 0);
    if (callResult != LUA_OK) {
        std::printf("[Items][Lua] on_pickup error for '%s': %s\n",
                    itemId.c_str(),
                    lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "?");
        lua_pop(luaState, 1);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// openItemScriptSession
// ---------------------------------------------------------------------------

ItemScriptSession openItemScriptSession(const ItemDefinition& definition) {
    ItemScriptSession session;

    if (!definition.script.enabled || definition.script.source.empty()) {
        return session;
    }

    if (!definition.script.loadError.empty()) {
        std::printf("[Items][Lua] %s\n", definition.script.loadError.c_str());
        return session;
    }

    lua_State* L = luaL_newstate();
    if (!L) {
        std::printf("[Items][Lua] Failed to create Lua state for session '%s'.\n",
                    definition.id.c_str());
        return session;
    }

    openSafeLibraries(L);

    std::string bootError;
    if (!loadAndBootScript(L, definition, &bootError)) {
        lua_close(L);
        return session;
    }

    // Detect available hooks.
    auto checkHook = [&](const char* name) -> bool {
        lua_getglobal(L, name);
        const bool exists = lua_isfunction(L, -1);
        lua_pop(L, 1);
        return exists;
    };

    session.luaState    = L;
    session.itemId      = definition.id;
    session.hasOnUse    = checkHook("on_use");
    session.hasOnUpdate = checkHook("on_update");
    session.hasOnPickup = checkHook("on_pickup");

    return session;
}

// ---------------------------------------------------------------------------
// runItemScriptOnUse — stateless fallback
// ---------------------------------------------------------------------------

bool runItemScriptOnUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError
) {
    if (!definition.script.enabled) {
        return false;
    }

    if (!definition.script.loadError.empty()) {
        if (outError) *outError = definition.script.loadError;
        std::printf("[Items][Lua] %s\n", definition.script.loadError.c_str());
        return false;
    }

    if (definition.script.source.empty()) {
        if (outError) *outError = "Item script source is empty.";
        std::printf("[Items][Lua] Item script source is empty for '%s'.\n",
                    definition.id.c_str());
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L) {
        if (outError) *outError = "Failed to create Lua state.";
        return false;
    }

    openSafeLibraries(L);

    if (!loadAndBootScript(L, definition, outError)) {
        lua_close(L);
        return false;
    }

    lua_getglobal(L, "on_use");
    if (!lua_isfunction(L, -1)) {
        if (outError) *outError = "Script does not define on_use(context).";
        std::printf("[Items][Lua] Script for '%s' does not define on_use(context).\n",
                    definition.id.c_str());
        lua_close(L);
        return false;
    }

    buildContextTable(L, context);

    const int callResult = lua_pcall(L, 1, 1, 0);
    if (callResult != LUA_OK) {
        const std::string err =
            lua_isstring(L, -1) ? lua_tostring(L, -1) : "Unknown call error.";
        if (outError) *outError = err;
        std::printf("[Items][Lua] on_use failed for '%s': %s\n",
                    definition.id.c_str(), err.c_str());
        lua_close(L);
        return false;
    }

    bool consumed = true;
    if (lua_isboolean(L, -1)) {
        consumed = lua_toboolean(L, -1) != 0;
    }

    lua_close(L);
    return consumed;
}
