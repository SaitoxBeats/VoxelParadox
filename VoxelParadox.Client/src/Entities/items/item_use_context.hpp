// File: VoxelParadox.Client/src/Entities/items/item_use_context.hpp
// Purpose: carries the controlled runtime bridge exposed to item use behaviors.
// Flow: declarative actions and Lua scripts read and mutate gameplay only through this context.

#pragma once

// 1. Third-party Libraries
#include <glm/glm.hpp>

// 2. Local Project Modules
#include "items/item_types.hpp"

class FractalWorld;
class GameAudioController;
class GameChat;
class Player;
class WorldStack;
class hudPortalTracker;

struct ItemUseContext {
    Player* player = nullptr;
    WorldStack* worldStack = nullptr;
    FractalWorld* world = nullptr;
    GameAudioController* audioController = nullptr;
    GameChat* gameChat = nullptr;
    hudPortalTracker* portalTracker = nullptr;
    bool hasTarget = false;
    glm::ivec3 targetBlock{ 0 };
    glm::ivec3 targetNormal{ 0 };
    BlockId targetBlockType = BlockIds::AIR;
    InventoryItem selectedItem{};
    int selectedCount = 0;
};
