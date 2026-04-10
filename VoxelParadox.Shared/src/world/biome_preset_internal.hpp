#pragma once

// biome_preset_internal.hpp
// Shared helpers for the biome preset implementation.
// Keep this header private to the biome preset .cpp files so the public API
// in biome_preset.hpp stays focused on data types and entry points.

#include "biome_preset.hpp"

namespace VoxelGame {

std::string sanitizePresetId(std::string value);
std::string sanitizeModuleId(std::string value);

PerlinDensityModule legacyDefaultPerlinDensity(int depth);
MaterialPaletteModule legacyDefaultMaterialPalette(int depth);
PerlinTerrainModule legacyDefaultPerlinTerrain(int depth);

} // namespace VoxelGame
