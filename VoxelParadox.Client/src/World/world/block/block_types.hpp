// File: VoxelParadox.Client/src/World/world/block/block_types.hpp
// Purpose: declares stable block ids, block categories, and low-level block metadata structs.
// Flow: consumed by the block registry and the shared block helper API.

#pragma once

// 1. Standard Library
#include <cstdint>

using BlockId = std::uint8_t;

enum class BlockCategory : std::uint32_t {
    NONE = 0u,
    TERRAIN = 1u << 0,
    DECORATION = 1u << 1,
    PORTAL = 1u << 2,
    PLANT = 1u << 3,
    ORGANIC = 1u << 4,
};

inline constexpr std::uint32_t BLOCK_CATEGORY_NONE =
    static_cast<std::uint32_t>(BlockCategory::NONE);
inline constexpr std::uint32_t BLOCK_CATEGORY_TERRAIN =
    static_cast<std::uint32_t>(BlockCategory::TERRAIN);
inline constexpr std::uint32_t BLOCK_CATEGORY_DECORATION =
    static_cast<std::uint32_t>(BlockCategory::DECORATION);
inline constexpr std::uint32_t BLOCK_CATEGORY_PORTAL =
    static_cast<std::uint32_t>(BlockCategory::PORTAL);
inline constexpr std::uint32_t BLOCK_CATEGORY_PLANT =
    static_cast<std::uint32_t>(BlockCategory::PLANT);
inline constexpr std::uint32_t BLOCK_CATEGORY_ORGANIC =
    static_cast<std::uint32_t>(BlockCategory::ORGANIC);

constexpr std::uint32_t BLOCK_TAG_NONE = 0u;
constexpr std::uint32_t BLOCK_TAG_MINEABLE_WITH_AXE = 1u << 0;
constexpr std::uint32_t BLOCK_TAG_MINEABLE_WITH_PICKAXE = 1u << 1;

struct BlockProperties {
    bool solid = false;
    bool transparent = false;
    bool interactable = false;
    bool emitsLight = false;
    bool hasEntity = false;
    std::uint32_t tags = BLOCK_TAG_NONE;
    float hardness = 0.0f;
};

struct BlockData {
    bool canDropItem = false;
    float breakExperienceMultiplier = 0.0f;
};
