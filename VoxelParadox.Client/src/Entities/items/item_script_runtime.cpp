// File: VoxelParadox.Client/src/Entities/items/item_script_runtime.cpp
// Purpose: provides a safe Lua runtime for item on-use scripts.
// Flow: creates a restricted Lua state, binds gameplay helpers through the item use context, and invokes on_use(context).

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

// --- 1. Lua Utilities ---

ItemUseContext* getLuaContext(lua_State* luaState) {
    ItemUseContext* context =
        static_cast<ItemUseContext*>(lua_touserdata(luaState, lua_upvalueindex(1)));
    return context;
}

bool readLuaNotificationColor(lua_State* luaState, int index, glm::vec3& outColor) {
    const int absIndex = lua_absindex(luaState, index);
    if (!lua_istable(luaState, absIndex)) {
        return false;
    }

    bool hasComponent = false;
    auto readComponent = [&](const char* fieldName, float& component) {
        lua_getfield(luaState, absIndex, fieldName);
        if (lua_isnumber(luaState, -1)) {
            component = static_cast<float>(lua_tonumber(luaState, -1));
            hasComponent = true;
        }
        lua_pop(luaState, 1);
    };

    readComponent("r", outColor.r);
    readComponent("g", outColor.g);
    readComponent("b", outColor.b);

    if (hasComponent) {
        return true;
    }

    for (int channel = 0; channel < 3; ++channel) {
        lua_rawgeti(luaState, absIndex, channel + 1);
        if (lua_isnumber(luaState, -1)) {
            outColor[channel] = static_cast<float>(lua_tonumber(luaState, -1));
            hasComponent = true;
        }
        lua_pop(luaState, 1);
    }

    return hasComponent;
}

bool readLuaNotificationSegment(lua_State* luaState, int index,
                                GameChatTextSegment& outSegment) {
    const int absIndex = lua_absindex(luaState, index);

    if (lua_isstring(luaState, absIndex)) {
        outSegment.text = lua_tostring(luaState, absIndex);
        outSegment.color = GameChatTheme::kDefaultHistoryColor;
        return !outSegment.text.empty();
    }

    if (!lua_istable(luaState, absIndex)) {
        return false;
    }

    lua_getfield(luaState, absIndex, "text");
    if (!lua_isstring(luaState, -1)) {
        lua_pop(luaState, 1);
        return false;
    }

    outSegment.text = lua_tostring(luaState, -1);
    lua_pop(luaState, 1);
    outSegment.color = GameChatTheme::kDefaultHistoryColor;

    lua_getfield(luaState, absIndex, "color");
    if (lua_istable(luaState, -1)) {
        glm::vec3 color = outSegment.color;
        if (readLuaNotificationColor(luaState, -1, color)) {
            outSegment.color = color;
        }
    }
    lua_pop(luaState, 1);

    return !outSegment.text.empty();
}

std::vector<GameChatTextSegment> readLuaNotificationSegments(lua_State* luaState, int index) {
    std::vector<GameChatTextSegment> segments;
    const int absIndex = lua_absindex(luaState, index);
    const std::size_t segmentCount = lua_rawlen(luaState, absIndex);

    for (std::size_t segmentIndex = 1; segmentIndex <= segmentCount; ++segmentIndex) {
        lua_rawgeti(luaState, absIndex, static_cast<lua_Integer>(segmentIndex));

        GameChatTextSegment segment{};
        if (readLuaNotificationSegment(luaState, -1, segment)) {
            segments.push_back(std::move(segment));
        }

        lua_pop(luaState, 1);
    }

    return segments;
}

int luaContextLog(lua_State* luaState) {
    const char* message = luaL_checkstring(luaState, 1);
    std::printf("%s\n", message);
    return 0;
}

int luaContextGetPlayerLife(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int lifePoints =
        (context && context->player) ? context->player->getLifePoints() : 0;
    lua_pushinteger(luaState, lifePoints);
    return 1;
}

int luaContextGetPlayerMaxLife(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int maxLifePoints =
        (context && context->player) ? context->player->getMaxLifePoints() : 0;
    lua_pushinteger(luaState, maxLifePoints);
    return 1;
}

int luaContextIsPlayerAlive(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const bool isAlive =
        context && context->player && context->player->isAlive();
    lua_pushboolean(luaState, isAlive ? 1 : 0);
    return 1;
}

int luaContextHealPlayer(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int amount = static_cast<int>(luaL_checkinteger(luaState, 1));

    if (context && context->player) {
        context->player->setLifePoints(context->player->getLifePoints() + amount);
    }

    return 0;
}

int luaContextPushNotification(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    if (!context || !context->gameChat) {
        return 0;
    }

    const int valueType = lua_type(luaState, 1);
    if (valueType == LUA_TSTRING) {
        context->gameChat->pushNotification(std::string(lua_tostring(luaState, 1)));
        return 0;
    }

    if (valueType != LUA_TTABLE) {
        return luaL_error(
            luaState,
            "push_notification expects a string or a table of segments."
        );
    }

    GameChatTextSegment singleSegment{};
    if (readLuaNotificationSegment(luaState, 1, singleSegment)) {
        context->gameChat->pushNotification(
            std::vector<GameChatTextSegment>{ std::move(singleSegment) }
        );
        return 0;
    }

    const std::vector<GameChatTextSegment> segments = readLuaNotificationSegments(luaState, 1);
    if (!segments.empty()) {
        context->gameChat->pushNotification(std::move(segments));
        return 0;
    }

    return luaL_error(
        luaState,
        "push_notification expects a string or a table of segments."
    );
}

int luaContextGetPlayerXp(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const float experiencePoints =
        (context && context->player) ? context->player->getExperiencePoints() : 0.0f;
    lua_pushnumber(luaState, static_cast<lua_Number>(experiencePoints));
    return 1;
}

int luaContextGetPlayerLevel(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int experienceLevel =
        (context && context->player) ? context->player->getExperienceLevel() : 0;
    lua_pushinteger(luaState, experienceLevel);
    return 1;
}

int luaContextSetPlayerXp(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const float experiencePoints = static_cast<float>(luaL_checknumber(luaState, 1));

    if (context && context->player) {
        context->player->setExperiencePoints(experiencePoints);
    }

    return 0;
}

int luaContextAddPlayerXp(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const float experiencePoints = static_cast<float>(luaL_checknumber(luaState, 1));

    if (context && context->player) {
        context->player->addExperiencePoints(experiencePoints, context->worldStack);
    }

    return 0;
}

int luaContextAddItem(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* rawItemId = luaL_checkstring(luaState, 1);
    const int amount = static_cast<int>(luaL_optinteger(luaState, 2, 1));

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool added =
        parsed && context && context->player &&
        context->player->tryAddItemToInventory(item, amount);

    lua_pushboolean(luaState, added ? 1 : 0);
    return 1;
}

int luaContextRemoveItem(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* rawItemId = luaL_checkstring(luaState, 1);
    const int amount = static_cast<int>(luaL_optinteger(luaState, 2, 1));

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool removed =
        parsed && context && context->player &&
        context->player->tryRemoveInventoryItem(item, amount);

    lua_pushboolean(luaState, removed ? 1 : 0);
    return 1;
}

int luaContextCountItem(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* rawItemId = luaL_checkstring(luaState, 1);

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const int count =
        parsed && context && context->player
            ? context->player->countInventoryItem(item)
            : 0;

    lua_pushinteger(luaState, count);
    return 1;
}

int luaContextConsumeSelected(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int amount = static_cast<int>(luaL_optinteger(luaState, 1, 1));
    const bool consumed =
        context && context->player &&
        context->player->tryConsumeSelectedInventoryItem(amount);

    lua_pushboolean(luaState, consumed ? 1 : 0);
    return 1;
}

int luaContextOpenPortalTracker(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    if (context && context->portalTracker &&
        !context->portalTracker->isMenuOpen()) {
        context->portalTracker->toggleMenu();
    }

    return 0;
}

int luaContextCreatePortalForTargetBlock(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const bool created =
        context && context->player && context->worldStack && context->hasTarget &&
        context->player->tryCreatePortalForTargetBlock(*context->worldStack);

    lua_pushboolean(luaState, created ? 1 : 0);
    return 1;
}

int luaContextPlayAudioEvent(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* rawEventName = luaL_checkstring(luaState, 1);

    if (context && context->audioController) {
        context->audioController->playItemEvent(rawEventName);
    }

    return 0;
}

int luaContextHasTarget(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    lua_pushboolean(luaState, (context && context->hasTarget) ? 1 : 0);
    return 1;
}

int luaContextGetTargetBlockId(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* blockId =
        (context && context->hasTarget) ? getBlockId(context->targetBlockType) : "air";
    lua_pushstring(luaState, blockId);
    return 1;
}

int luaContextGetTargetBlockPosition(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const glm::ivec3 block =
        context ? context->targetBlock : glm::ivec3(0);

    lua_pushinteger(luaState, block.x);
    lua_pushinteger(luaState, block.y);
    lua_pushinteger(luaState, block.z);
    return 3;
}

int luaContextGetTargetNormal(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const glm::ivec3 normal =
        context ? context->targetNormal : glm::ivec3(0);

    lua_pushinteger(luaState, normal.x);
    lua_pushinteger(luaState, normal.y);
    lua_pushinteger(luaState, normal.z);
    return 3;
}

int luaContextGetCurrentWorldSeed(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const unsigned int seed =
        (context && context->world) ? context->world->seed : 0u;

    lua_pushinteger(luaState, static_cast<lua_Integer>(seed));
    return 1;
}

int luaContextGetSelectedItemId(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const char* itemId = "none";

    if (context) {
        itemId = getInventoryItemId(context->selectedItem);
    }

    lua_pushstring(luaState, itemId);
    return 1;
}

int luaContextGetSelectedCount(lua_State* luaState) {
    ItemUseContext* context = getLuaContext(luaState);
    const int count = context ? context->selectedCount : 0;
    lua_pushinteger(luaState, count);
    return 1;
}

void pushContextFunction(
    lua_State* luaState,
    ItemUseContext* context,
    const char* functionName,
    lua_CFunction functionPointer
) {
    lua_pushstring(luaState, functionName);
    lua_pushlightuserdata(luaState, context);
    lua_pushcclosure(luaState, functionPointer, 1);
    lua_settable(luaState, -3);
}

void pushContextSnapshot(lua_State* luaState, const ItemUseContext& context) {
    lua_pushstring(luaState, "selected_item_id");
    lua_pushstring(luaState, getInventoryItemId(context.selectedItem));
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "selected_count");
    lua_pushinteger(luaState, context.selectedCount);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "has_target");
    lua_pushboolean(luaState, context.hasTarget ? 1 : 0);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "target_block_id");
    lua_pushstring(luaState, getBlockId(context.targetBlockType));
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "current_world_seed");
    lua_pushinteger(luaState, context.world ? static_cast<lua_Integer>(context.world->seed) : 0);
    lua_settable(luaState, -3);
}

void buildContextTable(lua_State* luaState, ItemUseContext& context) {
    lua_newtable(luaState);

    pushContextSnapshot(luaState, context);

    pushContextFunction(luaState, &context, "log", luaContextLog);
    pushContextFunction(luaState, &context, "push_notification", luaContextPushNotification);
    pushContextFunction(luaState, &context, "get_player_life", luaContextGetPlayerLife);
    pushContextFunction(luaState, &context, "get_player_max_life", luaContextGetPlayerMaxLife);
    pushContextFunction(luaState, &context, "is_player_alive", luaContextIsPlayerAlive);
    pushContextFunction(luaState, &context, "heal_player", luaContextHealPlayer);
    pushContextFunction(luaState, &context, "get_player_xp", luaContextGetPlayerXp);
    pushContextFunction(luaState, &context, "get_player_level", luaContextGetPlayerLevel);
    pushContextFunction(luaState, &context, "set_player_xp", luaContextSetPlayerXp);
    pushContextFunction(luaState, &context, "add_player_xp", luaContextAddPlayerXp);
    pushContextFunction(luaState, &context, "add_item", luaContextAddItem);
    pushContextFunction(luaState, &context, "remove_item", luaContextRemoveItem);
    pushContextFunction(luaState, &context, "count_item", luaContextCountItem);
    pushContextFunction(luaState, &context, "consume_selected", luaContextConsumeSelected);
    pushContextFunction(luaState, &context, "open_portal_tracker", luaContextOpenPortalTracker);
    pushContextFunction(luaState, &context, "create_portal_for_target_block",
                        luaContextCreatePortalForTargetBlock);
    pushContextFunction(luaState, &context, "play_audio_event", luaContextPlayAudioEvent);
    pushContextFunction(luaState, &context, "has_target", luaContextHasTarget);
    pushContextFunction(luaState, &context, "get_target_block_id", luaContextGetTargetBlockId);
    pushContextFunction(luaState, &context, "get_target_block_position", luaContextGetTargetBlockPosition);
    pushContextFunction(luaState, &context, "get_target_normal", luaContextGetTargetNormal);
    pushContextFunction(luaState, &context, "get_current_world_seed", luaContextGetCurrentWorldSeed);
    pushContextFunction(luaState, &context, "get_selected_item_id", luaContextGetSelectedItemId);
    pushContextFunction(luaState, &context, "get_selected_count", luaContextGetSelectedCount);
}

void openSafeLibraries(lua_State* luaState) {
    luaL_requiref(luaState, "_G", luaopen_base, 1);
    lua_pop(luaState, 1);

    luaL_requiref(luaState, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(luaState, 1);

    luaL_requiref(luaState, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(luaState, 1);

    luaL_requiref(luaState, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(luaState, 1);
}

} // namespace

bool runItemScriptOnUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError
) {
    // --- 1. Validate Script ---
    if (!definition.script.enabled) {
        return false;
    }

    if (!definition.script.loadError.empty()) {
        if (outError) {
            *outError = definition.script.loadError;
        }
        std::printf("[Items][Lua] %s\n", definition.script.loadError.c_str());
        return false;
    }

    if (definition.script.source.empty()) {
        if (outError) {
            *outError = "Item script source is empty.";
        }
        std::printf("[Items][Lua] Item script source is empty for '%s'.\n",
                    definition.id.c_str());
        return false;
    }

    // --- 2. Build Runtime ---
    lua_State* luaState = luaL_newstate();
    if (luaState == nullptr) {
        if (outError) {
            *outError = "Failed to create Lua state.";
        }
        return false;
    }

    openSafeLibraries(luaState);

    const int loadResult = luaL_loadbufferx(
        luaState,
        definition.script.source.c_str(),
        definition.script.source.size(),
        definition.script.relativePath.c_str(),
        "t"
    );

    if (loadResult != LUA_OK) {
        const std::string luaError =
            lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "Unknown load error.";
        if (outError) {
            *outError = luaError;
        }
        std::printf("[Items][Lua] Failed to load script for '%s': %s\n",
                    definition.id.c_str(),
                    luaError.c_str());
        lua_close(luaState);
        return false;
    }

    const int bootResult = lua_pcall(luaState, 0, 0, 0);
    if (bootResult != LUA_OK) {
        const std::string luaError =
            lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "Unknown runtime error.";
        if (outError) {
            *outError = luaError;
        }
        std::printf("[Items][Lua] Failed to bootstrap script for '%s': %s\n",
                    definition.id.c_str(),
                    luaError.c_str());
        lua_close(luaState);
        return false;
    }

    // --- 3. Invoke on_use ---
    lua_getglobal(luaState, "on_use");
    if (!lua_isfunction(luaState, -1)) {
        if (outError) {
            *outError = "Script does not define on_use(context).";
        }
        std::printf("[Items][Lua] Script for '%s' does not define on_use(context).\n",
                    definition.id.c_str());
        lua_close(luaState);
        return false;
    }

    buildContextTable(luaState, context);

    const int callResult = lua_pcall(luaState, 1, 1, 0);
    if (callResult != LUA_OK) {
        const std::string luaError =
            lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "Unknown call error.";
        if (outError) {
            *outError = luaError;
        }
        std::printf("[Items][Lua] Script on_use failed for '%s': %s\n",
                    definition.id.c_str(),
                    luaError.c_str());
        lua_close(luaState);
        return false;
    }

    bool consumed = true;
    if (lua_isboolean(luaState, -1)) {
        consumed = lua_toboolean(luaState, -1) != 0;
    }

    lua_close(luaState);
    return consumed;
}
