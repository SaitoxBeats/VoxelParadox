// File: VoxelParadox.Client/src/Systems/gameplay/block_script_runtime.hpp
// Purpose: executes Lua-backed block scripts for gameplay events.
// Flow: block interaction systems build a BlockScriptContext and delegate optional block scripts here.

#pragma once

// 1. Standard Library
#include <string>

// 2. Local Project Modules
#include "gameplay/gameplay_script_contexts.hpp"
#include "world/block/block_registry.hpp"

namespace Gameplay {

bool runBlockScriptOnBreak(
    const BlockDefinition& definition,
    BlockScriptContext& context,
    std::string* outError = nullptr
);

}  // namespace Gameplay
