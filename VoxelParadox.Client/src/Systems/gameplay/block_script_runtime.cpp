// File: VoxelParadox.Client/src/Systems/gameplay/block_script_runtime.cpp
// Purpose: provides a safe Lua runtime for block gameplay scripts.
// Flow: creates a restricted Lua state, exposes BlockScriptContext helpers, and invokes on_break(context).

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
#include "gameplay/block_script_runtime.hpp"

#include "items/item_catalog.hpp"
#include "player/player.hpp"
#include "runtime/state/game_chat.hpp"
#include "world/block/block.hpp"
#include "world/generation/fractal_world.hpp"

namespace {

// --- 1. Lua Utilities ---

Gameplay::BlockScriptContext* getLuaContext(lua_State* luaState) {
    auto* context = static_cast<Gameplay::BlockScriptContext*>(
        lua_touserdata(luaState, lua_upvalueindex(1))
    );
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

bool readLuaNotificationSegment(
    lua_State* luaState,
    int index,
    GameChatTextSegment& outSegment
) {
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

void pushContextFunction(
    lua_State* luaState,
    Gameplay::BlockScriptContext* context,
    const char* functionName,
    lua_CFunction functionPointer
) {
    lua_pushstring(luaState, functionName);
    lua_pushlightuserdata(luaState, context);
    lua_pushcclosure(luaState, functionPointer, 1);
    lua_settable(luaState, -3);
}

// --- 2. Context Functions ---

int luaContextLog(lua_State* luaState) {
    const char* message = luaL_checkstring(luaState, 1);
    std::printf("%s\n", message);
    return 0;
}

int luaContextPushNotification(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
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

    const std::vector<GameChatTextSegment> segments =
        readLuaNotificationSegments(luaState, 1);
    if (!segments.empty()) {
        context->gameChat->pushNotification(std::move(segments));
        return 0;
    }

    return luaL_error(
        luaState,
        "push_notification expects a string or a table of segments."
    );
}

int luaContextGetBlockId(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const char* blockId = context ? getBlockId(context->blockType) : "air";
    lua_pushstring(luaState, blockId);
    return 1;
}

int luaContextGetBlockPosition(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const glm::ivec3 block = context ? context->blockPosition : glm::ivec3(0);

    lua_pushinteger(luaState, block.x);
    lua_pushinteger(luaState, block.y);
    lua_pushinteger(luaState, block.z);
    return 3;
}

int luaContextGetBlockNormal(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const glm::ivec3 normal = context ? context->blockNormal : glm::ivec3(0);

    lua_pushinteger(luaState, normal.x);
    lua_pushinteger(luaState, normal.y);
    lua_pushinteger(luaState, normal.z);
    return 3;
}

int luaContextGetCurrentWorldSeed(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const unsigned int seed =
        (context && context->world) ? context->world->seed : 0u;

    lua_pushinteger(luaState, static_cast<lua_Integer>(seed));
    return 1;
}

int luaContextGetPlayerXp(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const float experiencePoints =
        (context && context->player) ? context->player->getExperiencePoints() : 0.0f;

    lua_pushnumber(luaState, static_cast<lua_Number>(experiencePoints));
    return 1;
}

int luaContextGetPlayerLevel(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const int experienceLevel =
        (context && context->player) ? context->player->getExperienceLevel() : 0;

    lua_pushinteger(luaState, experienceLevel);
    return 1;
}

int luaContextAddPlayerXp(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const float experiencePoints = static_cast<float>(luaL_checknumber(luaState, 1));

    if (context) {
        context->addPlayerExperience(experiencePoints);
    }

    return 0;
}

int luaContextAddItem(lua_State* luaState) {
    Gameplay::BlockScriptContext* context = getLuaContext(luaState);
    const char* rawItemId = luaL_checkstring(luaState, 1);
    const int amount = static_cast<int>(luaL_optinteger(luaState, 2, 1));

    InventoryItem item{};
    const bool parsed = tryParseInventoryItem(rawItemId, item);
    const bool added =
        parsed && context && context->tryAddInventoryItem(item, amount);

    lua_pushboolean(luaState, added ? 1 : 0);
    return 1;
}

void pushContextSnapshot(
    lua_State* luaState,
    const Gameplay::BlockScriptContext& context
) {
    lua_pushstring(luaState, "block_id");
    lua_pushstring(luaState, getBlockId(context.blockType));
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_position_x");
    lua_pushinteger(luaState, context.blockPosition.x);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_position_y");
    lua_pushinteger(luaState, context.blockPosition.y);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_position_z");
    lua_pushinteger(luaState, context.blockPosition.z);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_normal_x");
    lua_pushinteger(luaState, context.blockNormal.x);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_normal_y");
    lua_pushinteger(luaState, context.blockNormal.y);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "block_normal_z");
    lua_pushinteger(luaState, context.blockNormal.z);
    lua_settable(luaState, -3);

    lua_pushstring(luaState, "current_world_seed");
    lua_pushinteger(
        luaState,
        context.world ? static_cast<lua_Integer>(context.world->seed) : 0
    );
    lua_settable(luaState, -3);
}

void buildContextTable(lua_State* luaState, Gameplay::BlockScriptContext& context) {
    lua_newtable(luaState);

    pushContextSnapshot(luaState, context);

    pushContextFunction(luaState, &context, "log", luaContextLog);
    pushContextFunction(luaState, &context, "push_notification", luaContextPushNotification);
    pushContextFunction(luaState, &context, "get_block_id", luaContextGetBlockId);
    pushContextFunction(luaState, &context, "get_block_position", luaContextGetBlockPosition);
    pushContextFunction(luaState, &context, "get_block_normal", luaContextGetBlockNormal);
    pushContextFunction(luaState, &context, "get_current_world_seed",
                        luaContextGetCurrentWorldSeed);
    pushContextFunction(luaState, &context, "get_player_xp", luaContextGetPlayerXp);
    pushContextFunction(luaState, &context, "get_player_level", luaContextGetPlayerLevel);
    pushContextFunction(luaState, &context, "add_player_xp", luaContextAddPlayerXp);
    pushContextFunction(luaState, &context, "add_item", luaContextAddItem);
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

}  // namespace

namespace Gameplay {

bool runBlockScriptOnBreak(
    const BlockDefinition& definition,
    BlockScriptContext& context,
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
        std::printf("[Blocks][Lua] %s\n", definition.script.loadError.c_str());
        return false;
    }

    if (definition.script.source.empty()) {
        if (outError) {
            *outError = "Block script source is empty.";
        }
        std::printf("[Blocks][Lua] Block script source is empty for '%s'.\n",
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
        std::printf("[Blocks][Lua] Failed to load script for '%s': %s\n",
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
        std::printf("[Blocks][Lua] Failed to bootstrap script for '%s': %s\n",
                    definition.id.c_str(),
                    luaError.c_str());
        lua_close(luaState);
        return false;
    }

    // --- 3. Invoke on_break ---
    lua_getglobal(luaState, "on_break");
    if (!lua_isfunction(luaState, -1)) {
        if (outError) {
            *outError = "Script does not define on_break(context).";
        }
        std::printf("[Blocks][Lua] Script for '%s' does not define on_break(context).\n",
                    definition.id.c_str());
        lua_close(luaState);
        return false;
    }

    buildContextTable(luaState, context);

    const int callResult = lua_pcall(luaState, 1, 0, 0);
    if (callResult != LUA_OK) {
        const std::string luaError =
            lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : "Unknown call error.";
        if (outError) {
            *outError = luaError;
        }
        std::printf("[Blocks][Lua] Script on_break failed for '%s': %s\n",
                    definition.id.c_str(),
                    luaError.c_str());
        lua_close(luaState);
        return false;
    }

    lua_close(luaState);
    return true;
}

}  // namespace Gameplay
