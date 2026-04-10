// File: VoxelParadox.Client/src/World/world/block_registry.hpp
// Purpose: declares the data-driven block registry used by gameplay, rendering, and generation.
// Flow: loads block definitions from data with a compiled fallback and exposes stable lookups.

#pragma once

// 1. Standard Library
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "block_catalog.hpp"

enum class BlockSupportMode {
    SOLID,
    ALLOW_LIST,
};

struct BlockSelectionBounds {
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 1.0f };
};

struct BlockSupportRule {
    BlockSupportMode mode = BlockSupportMode::SOLID;
    std::vector<BlockId> allowedSupportTypes{};
};

struct BlockTopDecorationRule {
    bool enabled = false;
    BlockId anchorBlockId = BlockIds::AIR;
    BlockId requiredAboveBlockId = BlockIds::AIR;
    float spawnChance = 0.0f;
    int verticalOffset = 1;
    std::string seedKey{};
};

struct BlockDefinition {
    BlockId idValue = BlockIds::AIR;
    std::string id{};
    std::string displayName{};
    std::uint32_t categoryMask = BLOCK_CATEGORY_NONE;
    BlockProperties properties{};
    BlockData data{};
    bool replaceable = false;
    bool placeable = false;
    bool targetable = false;
    bool requiresTopPlacement = false;
    bool hasCustomSelectionBounds = false;
    BlockSelectionBounds selectionBounds{};
    BlockSupportRule supportRule{};
    glm::vec3 baseColor{ 1.0f, 0.0f, 1.0f };
    int materialId = 0;
    std::string shaderAssetPath{};
    std::string shaderSource{};
    std::string textureAssetPath{};
    std::string textureTileId{};
    bool hasTextureTile = false;
    glm::vec4 atlasUvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
    std::string customModelAssetPath{};
    BlockTopDecorationRule topDecoration{};
};

struct BlockShaderSources {
    std::string vertexSource{};
    std::string fragmentSource{};
    std::string fallbackFragmentSource{};
    std::string error{};

    bool valid() const {
        return !vertexSource.empty() && !fragmentSource.empty();
    }
};

class BlockRegistry {
public:
    static const BlockRegistry& instance();
    static BlockRegistry& mutableInstance();

    const BlockDefinition& definition(BlockId blockId) const;
    const std::vector<BlockDefinition>& definitions() const;
    const std::vector<const BlockDefinition*>& topDecorationDefinitions() const;
    const std::string& textureAtlasAssetPath() const;

    bool tryParseId(const std::string& rawValue, BlockId& outBlockId) const;
    BlockShaderSources buildShaderSources() const;
    void reload();

private:
    BlockRegistry();

    std::vector<BlockDefinition> definitions_{};
    std::vector<const BlockDefinition*> topDecorationDefinitions_{};
    std::string textureAtlasAssetPath_{};
};
