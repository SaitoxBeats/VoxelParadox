// File: VoxelParadox.Client/src/Entities/items/item_action_runtime.hpp
// Purpose: executes declarative item actions defined in item.json.
// Flow: item behaviors delegate on-use processing here so simple items do not need custom C++ classes.

#pragma once

// 1. Standard Library
#include <string>

// 2. Local Project Modules
#include "items/item_registry.hpp"
#include "items/item_use_context.hpp"

bool runDeclarativeItemUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError = nullptr
);

bool beginDeclarativeItemUse(
    const ItemDefinition& definition,
    const ItemUseContext& context,
    std::string* outError = nullptr
);

bool executeDeclarativeItemActions(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError = nullptr
);

bool finalizeDeclarativeItemUse(
    const ItemDefinition& definition,
    ItemUseContext& context,
    std::string* outError = nullptr
);
