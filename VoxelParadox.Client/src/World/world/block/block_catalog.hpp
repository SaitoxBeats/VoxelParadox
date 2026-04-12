// File: VoxelParadox.Client/src/World/world/block/block_catalog.hpp
// Purpose: exposes the stable block id catalog loaded from Assets/Blocks/registry.json.
// Flow: keeps named block references data-driven while preserving the existing call surface.

#pragma once

// 1. Standard Library
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// 2. Local Project Modules
#include "world/block/block_types.hpp"

struct BlockCatalogEntry {
    BlockId value = 0;
    std::string stableId{};
    std::string folderName{};
};

namespace BlockCatalog {

const std::vector<BlockCatalogEntry>& entries();
const BlockCatalogEntry* findByValue(BlockId blockId);
const BlockCatalogEntry* findByStableId(std::string_view stableId);
bool tryResolve(std::string_view stableId, BlockId& outBlockId);
BlockId require(std::string_view stableId);
std::size_t count();

} // namespace BlockCatalog

struct NamedBlockIdRef {
    const char* stableId = "";

    operator BlockId() const;
};

struct BlockCountRef {
    operator BlockId() const;
};

namespace BlockIds {

inline constexpr NamedBlockIdRef AIR{ "air" };
inline constexpr NamedBlockIdRef STONE{ "stone" };
inline constexpr NamedBlockIdRef CRYSTAL{ "crystal" };
inline constexpr NamedBlockIdRef VOID_MATTER{ "void_matter" };
inline constexpr NamedBlockIdRef MEMBRANE{ "membrane" };
inline constexpr NamedBlockIdRef ORGANIC{ "organic" };
inline constexpr NamedBlockIdRef METAL{ "metal" };
inline constexpr NamedBlockIdRef PORTAL{ "portal" };
inline constexpr NamedBlockIdRef MEMBRANE_WEAVE{ "membrane_weave" };
inline constexpr NamedBlockIdRef MEMBRANE_WIRE{ "membrane_wire" };
inline constexpr BlockCountRef COUNT{};

} // namespace BlockIds
