// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_script_contexts.hpp
// Purpose: defines specialized script contexts built on top of the reusable Gameplay::ScriptContext.
// Flow: future block, portal, and biome scripts can receive focused context data without duplicating runtime services.

#pragma once

// 1. Standard Library
#include <string>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "gameplay/gameplay_script_context.hpp"
#include "world/biome/biome.hpp"

namespace Gameplay {

struct BlockScriptContext : ScriptContext {
    glm::ivec3 blockPosition{ 0 };
    glm::ivec3 blockNormal{ 0 };
    BlockId blockType = BlockIds::AIR;

    void setBlockState(
        const glm::ivec3& position,
        const glm::ivec3& normal,
        BlockId type
    ) {
        blockPosition = position;
        blockNormal = normal;
        blockType = type;
        setTargetState(true, position, normal, type);
    }
};

struct PortalScriptContext : ScriptContext {
    glm::ivec3 portalBlock{ 0 };
    glm::ivec3 portalNormal{ 0 };
    bool portalCreated = false;

    void setPortalState(
        const glm::ivec3& block,
        const glm::ivec3& normal,
        bool created
    ) {
        portalBlock = block;
        portalNormal = normal;
        portalCreated = created;
        setTargetState(true, block, normal, BlockIds::PORTAL);
    }
};

struct BiomeScriptContext : ScriptContext {
    BiomeSelection biomeSelection{};
    std::string biomeName;

    void setBiomeState(
        const BiomeSelection& selection,
        const std::string& name
    ) {
        biomeSelection = selection;
        biomeName = name;
    }
};

}  // namespace Gameplay
