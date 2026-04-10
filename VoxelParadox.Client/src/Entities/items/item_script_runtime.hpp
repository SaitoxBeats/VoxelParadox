// File: VoxelParadox.Client/src/Entities/items/item_script_runtime.hpp
// Purpose: executes Lua-backed item scripts for on-use behaviors.
// Flow: script-capable item behaviors delegate to this runtime after any declarative actions complete.

#pragma once

// 1. Standard Library
#include <string>

// 2. Local Project Modules
#include "items/item_registry.hpp"
#include "items/item_use_context.hpp"

bool runItemScriptOnUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError = nullptr
);
