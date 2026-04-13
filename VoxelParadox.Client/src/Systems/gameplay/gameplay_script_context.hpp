// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_script_context.hpp
// Purpose: defines the reusable gameplay-facing base context shared by scriptable systems.
// Flow: specialized contexts copy shared runtime services from Gameplay::Context and add subsystem data.

#pragma once

// 1. Standard Library

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "gameplay/gameplay_context.hpp"

class FractalWorld;

namespace Gameplay {

struct ScriptContext {
    Player* player = nullptr;
    WorldStack* worldStack = nullptr;
    FractalWorld* world = nullptr;
    GameAudioController* audioController = nullptr;
    GameChat* gameChat = nullptr;
    hudPortalTracker* portalTracker = nullptr;
    EventQueue* eventQueue = nullptr;
    bool hasTarget = false;
    glm::ivec3 targetBlock{ 0 };
    glm::ivec3 targetNormal{ 0 };
    BlockId targetBlockType = BlockIds::AIR;

    void inheritFrom(const Context& gameplayContext, FractalWorld* currentWorld);

    void clearTargetState();
    void setTargetState(
        bool hasTargetValue,
        const glm::ivec3& blockPosition,
        const glm::ivec3& blockNormal,
        BlockId blockTypeValue
    );
    void inheritTargetFromPlayer(FractalWorld* currentWorld = nullptr);

    bool tryAddInventoryItem(const InventoryItem& item, int amount = 1) const;
    bool tryRemoveInventoryItem(const InventoryItem& item, int amount = 1) const;
    int countInventoryItem(const InventoryItem& item) const;
    bool tryConsumeSelectedInventoryItem(int amount = 1) const;
    void setPlayerLifePoints(int lifePoints) const;
    void setPlayerExperience(float value) const;
    void addPlayerExperience(float value) const;
    bool tryCreatePortalForTargetBlock() const;
};

}  // namespace Gameplay
