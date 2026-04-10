// biome_preset_defaults.cpp
// Unity mental model: this is the "ScriptableObject defaults and helpers" side
// of biome presets. It owns ids, display names, factory builders, and lookup
// helpers. JSON/file IO stays in biome_preset.cpp.

#include "biome_preset_internal.hpp"

#include <algorithm>
#include <utility>

namespace VoxelGame {
namespace {

BiomeModule makeBaseModule(const std::string& id, std::string displayName,
                           ModuleType type, LayerBlendMode blendMode) {
  BiomeModule result;
  result.id = sanitizeModuleId(id);
  result.displayName = std::move(displayName);
  result.type = type;
  result.enabled = true;
  result.blendMode = blendMode;
  return result;
}

TricolorPaletteModule makeStoneMembraneCrystalPalette() {
  TricolorPaletteModule palette;
  palette.primary = BlockType::STONE;
  palette.secondary = BlockType::MEMBRANE;
  palette.accent = BlockType::CRYSTAL;
  return palette;
}

TricolorPaletteModule makeStoneOrganicCrystalPalette() {
  TricolorPaletteModule palette;
  palette.primary = BlockType::STONE;
  palette.secondary = BlockType::ORGANIC;
  palette.accent = BlockType::CRYSTAL;
  return palette;
}

VolumeNoiseModuleSettings makeDefaultVolumeNoiseSettings() {
  VolumeNoiseModuleSettings settings;
  settings.palette = makeStoneMembraneCrystalPalette();
  return settings;
}

std::string sanitizeIdentifier(std::string value, const char* fallback) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                     return static_cast<char>(ch);
                   }
                   if (ch >= 'A' && ch <= 'Z') {
                     return static_cast<char>(ch - 'A' + 'a');
                   }
                   return static_cast<char>('_');
                 });

  value.erase(std::unique(value.begin(), value.end(),
                          [](char a, char b) { return a == '_' && b == '_'; }),
              value.end());
  while (!value.empty() && value.front() == '_') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '_') {
    value.pop_back();
  }

  if (value.empty()) {
    value = fallback;
  }
  return value;
}

} // namespace

const char* previewModeId(PreviewMode mode) {
  switch (mode) {
  case PreviewMode::SANDBOX:
    return "sandbox";
  case PreviewMode::STREAMING:
    return "streaming";
  default:
    return "sandbox";
  }
}

const char* previewModeDisplayName(PreviewMode mode) {
  switch (mode) {
  case PreviewMode::SANDBOX:
    return "Sandbox";
  case PreviewMode::STREAMING:
    return "Streaming";
  default:
    return "Sandbox";
  }
}

bool tryParsePreviewMode(const std::string& value, PreviewMode& outMode) {
  if (value == "sandbox") {
    outMode = PreviewMode::SANDBOX;
    return true;
  }
  if (value == "streaming") {
    outMode = PreviewMode::STREAMING;
    return true;
  }
  outMode = PreviewMode::SANDBOX;
  return false;
}

const char* layerBlendModeId(LayerBlendMode mode) {
  switch (mode) {
  case LayerBlendMode::OVERWRITE_ALL:
    return "overwrite_all";
  case LayerBlendMode::PLACE_SOLIDS:
    return "place_solids";
  case LayerBlendMode::PLACE_ON_AIR:
    return "place_on_air";
  default:
    return "overwrite_all";
  }
}

const char* layerBlendModeDisplayName(LayerBlendMode mode) {
  switch (mode) {
  case LayerBlendMode::OVERWRITE_ALL:
    return "Overwrite All";
  case LayerBlendMode::PLACE_SOLIDS:
    return "Place Solids";
  case LayerBlendMode::PLACE_ON_AIR:
    return "Place On Air";
  default:
    return "Overwrite All";
  }
}

bool tryParseLayerBlendMode(const std::string& value, LayerBlendMode& outMode) {
  if (value == "overwrite_all") {
    outMode = LayerBlendMode::OVERWRITE_ALL;
    return true;
  }
  if (value == "place_solids") {
    outMode = LayerBlendMode::PLACE_SOLIDS;
    return true;
  }
  if (value == "place_on_air") {
    outMode = LayerBlendMode::PLACE_ON_AIR;
    return true;
  }
  outMode = LayerBlendMode::OVERWRITE_ALL;
  return false;
}

const char* moduleTypeId(ModuleType type) {
  switch (type) {
  case ModuleType::PERLIN_TERRAIN:
    return "perlin_terrain";
  case ModuleType::IMPORT_VOX_FILES:
    return "import_vox_files";
  case ModuleType::GRID_PATTERN:
    return "grid_pattern";
  case ModuleType::MENGER_SPONGE:
    return "menger_sponge";
  case ModuleType::CAVE_SYSTEM:
    return "cave_system";
  case ModuleType::CELLULAR_NOISE:
    return "cellular_noise";
  case ModuleType::FRACTAL_NOISE:
    return "fractal_noise";
  case ModuleType::RIDGED_NOISE:
    return "ridged_noise";
  case ModuleType::DOMAIN_WARPED_NOISE:
    return "domain_warped_noise";
  case ModuleType::TREE_GENERATOR:
    return "tree_generator";
  default:
    return "unknown";
  }
}

const char* moduleTypeDisplayName(ModuleType type) {
  switch (type) {
  case ModuleType::PERLIN_TERRAIN:
    return "Perlin";
  case ModuleType::IMPORT_VOX_FILES:
    return "Vox Import";
  case ModuleType::GRID_PATTERN:
    return "Grid Pattern";
  case ModuleType::MENGER_SPONGE:
    return "Menger Sponge";
  case ModuleType::CAVE_SYSTEM:
    return "Cave System";
  case ModuleType::CELLULAR_NOISE:
    return "Cellular Noise";
  case ModuleType::FRACTAL_NOISE:
    return "Fractal Noise";
  case ModuleType::RIDGED_NOISE:
    return "Ridged Noise";
  case ModuleType::DOMAIN_WARPED_NOISE:
    return "Domain Warped Noise";
  case ModuleType::TREE_GENERATOR:
    return "Tree Generator";
  default:
    return "Unknown";
  }
}

bool tryParseModuleType(const std::string& value, ModuleType& outType) {
  if (value == "perlin_terrain") {
    outType = ModuleType::PERLIN_TERRAIN;
    return true;
  }
  if (value == "import_vox_files") {
    outType = ModuleType::IMPORT_VOX_FILES;
    return true;
  }
  if (value == "grid_pattern") {
    outType = ModuleType::GRID_PATTERN;
    return true;
  }
  if (value == "menger_sponge") {
    outType = ModuleType::MENGER_SPONGE;
    return true;
  }
  if (value == "cave_system") {
    outType = ModuleType::CAVE_SYSTEM;
    return true;
  }
  if (value == "cellular_noise") {
    outType = ModuleType::CELLULAR_NOISE;
    return true;
  }
  if (value == "fractal_noise") {
    outType = ModuleType::FRACTAL_NOISE;
    return true;
  }
  if (value == "ridged_noise") {
    outType = ModuleType::RIDGED_NOISE;
    return true;
  }
  if (value == "domain_warped_noise") {
    outType = ModuleType::DOMAIN_WARPED_NOISE;
    return true;
  }
  if (value == "tree_generator") {
    outType = ModuleType::TREE_GENERATOR;
    return true;
  }
  outType = ModuleType::PERLIN_TERRAIN;
  return false;
}

const char* voxPlacementPatternId(VoxPlacementPattern pattern) {
  switch (pattern) {
  case VoxPlacementPattern::RANDOM_SCATTER:
    return "random_scatter";
  case VoxPlacementPattern::GRID:
    return "grid";
  default:
    return "random_scatter";
  }
}

const char* voxPlacementPatternDisplayName(VoxPlacementPattern pattern) {
  switch (pattern) {
  case VoxPlacementPattern::RANDOM_SCATTER:
    return "Random Scatter";
  case VoxPlacementPattern::GRID:
    return "Grid";
  default:
    return "Random Scatter";
  }
}

bool tryParseVoxPlacementPattern(const std::string& value,
                                 VoxPlacementPattern& outPattern) {
  if (value == "random_scatter") {
    outPattern = VoxPlacementPattern::RANDOM_SCATTER;
    return true;
  }
  if (value == "grid") {
    outPattern = VoxPlacementPattern::GRID;
    return true;
  }
  outPattern = VoxPlacementPattern::RANDOM_SCATTER;
  return false;
}

const char* treeGeneratorTypeId(TreeGeneratorType type) {
  switch (type) {
  case TreeGeneratorType::NORMAL:
    return "normal";
  case TreeGeneratorType::STRANGE:
    return "strange";
  case TreeGeneratorType::TRUNK_ONLY:
    return "trunk_only";
  default:
    return "normal";
  }
}

const char* treeGeneratorTypeDisplayName(TreeGeneratorType type) {
  switch (type) {
  case TreeGeneratorType::NORMAL:
    return "Normal";
  case TreeGeneratorType::STRANGE:
    return "Strange";
  case TreeGeneratorType::TRUNK_ONLY:
    return "Trunk Only";
  default:
    return "Normal";
  }
}

bool tryParseTreeGeneratorType(const std::string& value,
                               TreeGeneratorType& outType) {
  if (value == "normal") {
    outType = TreeGeneratorType::NORMAL;
    return true;
  }
  if (value == "strange") {
    outType = TreeGeneratorType::STRANGE;
    return true;
  }
  if (value == "trunk_only") {
    outType = TreeGeneratorType::TRUNK_ONLY;
    return true;
  }
  outType = TreeGeneratorType::NORMAL;
  return false;
}

const char* voxRotationModeId(VoxRotationMode mode) {
  switch (mode) {
  case VoxRotationMode::FIXED:
    return "fixed";
  case VoxRotationMode::RANDOM_90:
    return "random_90";
  default:
    return "random_90";
  }
}

const char* voxRotationModeDisplayName(VoxRotationMode mode) {
  switch (mode) {
  case VoxRotationMode::FIXED:
    return "Fixed";
  case VoxRotationMode::RANDOM_90:
    return "Random 90";
  default:
    return "Random 90";
  }
}

bool tryParseVoxRotationMode(const std::string& value, VoxRotationMode& outMode) {
  if (value == "fixed") {
    outMode = VoxRotationMode::FIXED;
    return true;
  }
  if (value == "random_90") {
    outMode = VoxRotationMode::RANDOM_90;
    return true;
  }
  outMode = VoxRotationMode::RANDOM_90;
  return false;
}

const char* voxColorMappingId(VoxColorMapping mapping) {
  switch (mapping) {
  case VoxColorMapping::DEFAULT:
    return "default";
  case VoxColorMapping::ANCIENT_RUINS:
    return "ancient_ruins";
  default:
    return "default";
  }
}

const char* voxColorMappingDisplayName(VoxColorMapping mapping) {
  switch (mapping) {
  case VoxColorMapping::DEFAULT:
    return "Default";
  case VoxColorMapping::ANCIENT_RUINS:
    return "Ancient Ruins";
  default:
    return "Default";
  }
}

bool tryParseVoxColorMapping(const std::string& value,
                             VoxColorMapping& outMapping) {
  if (value == "default") {
    outMapping = VoxColorMapping::DEFAULT;
    return true;
  }
  if (value == "ancient_ruins") {
    outMapping = VoxColorMapping::ANCIENT_RUINS;
    return true;
  }
  outMapping = VoxColorMapping::DEFAULT;
  return false;
}

BiomeModule BiomeModule::makePerlinTerrain(const std::string& id, int depth) {
  BiomeModule result =
      makeBaseModule(id, "Perlin", ModuleType::PERLIN_TERRAIN,
                     LayerBlendMode::OVERWRITE_ALL);
  result.perlinTerrain = legacyDefaultPerlinTerrain(depth);
  return result;
}

BiomeModule BiomeModule::makeImportVoxFiles(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Vox Import", ModuleType::IMPORT_VOX_FILES,
                     LayerBlendMode::PLACE_SOLIDS);
  result.importVoxFiles = {};
  result.importVoxFiles.defaultVoxel = BlockType::STONE;
  return result;
}

BiomeModule BiomeModule::makeGridPattern(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Grid Pattern", ModuleType::GRID_PATTERN,
                     LayerBlendMode::PLACE_SOLIDS);
  result.gridPattern = {};
  result.gridPattern.palette = makeStoneMembraneCrystalPalette();
  return result;
}

BiomeModule BiomeModule::makeMengerSponge(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Menger Sponge", ModuleType::MENGER_SPONGE,
                     LayerBlendMode::PLACE_SOLIDS);
  result.mengerSponge = {};
  result.mengerSponge.palette = makeStoneOrganicCrystalPalette();
  return result;
}

BiomeModule BiomeModule::makeCaveSystem(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Cave System", ModuleType::CAVE_SYSTEM,
                     LayerBlendMode::OVERWRITE_ALL);
  result.caveSystem = {};
  result.caveSystem.palette = makeStoneMembraneCrystalPalette();
  return result;
}

BiomeModule BiomeModule::makeCellularNoise(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Cellular Noise", ModuleType::CELLULAR_NOISE,
                     LayerBlendMode::PLACE_SOLIDS);
  result.cellularNoise = {};
  result.cellularNoise.noise = makeDefaultVolumeNoiseSettings();
  result.cellularNoise.noise.baseScale = 0.050f;
  result.cellularNoise.noise.threshold = 0.10f;
  result.cellularNoise.noise.secondaryThreshold = 0.08f;
  result.cellularNoise.distanceBlend = 0.20f;
  return result;
}

BiomeModule BiomeModule::makeFractalNoise(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Fractal Noise", ModuleType::FRACTAL_NOISE,
                     LayerBlendMode::PLACE_SOLIDS);
  result.fractalNoise = {};
  result.fractalNoise.noise = makeDefaultVolumeNoiseSettings();
  result.fractalNoise.noise.palette = makeStoneOrganicCrystalPalette();
  result.fractalNoise.noise.baseScale = 0.032f;
  result.fractalNoise.noise.octaves = 5;
  result.fractalNoise.noise.persistence = 0.52f;
  result.fractalNoise.noise.threshold = 0.02f;
  return result;
}

BiomeModule BiomeModule::makeRidgedNoise(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Ridged Noise", ModuleType::RIDGED_NOISE,
                     LayerBlendMode::PLACE_SOLIDS);
  result.ridgedNoise = {};
  result.ridgedNoise.noise = makeDefaultVolumeNoiseSettings();
  result.ridgedNoise.noise.baseScale = 0.028f;
  result.ridgedNoise.noise.octaves = 5;
  result.ridgedNoise.noise.persistence = 0.55f;
  result.ridgedNoise.noise.threshold = 0.24f;
  result.ridgedNoise.noise.secondaryThreshold = 0.05f;
  result.ridgedNoise.ridgeSharpness = 1.7f;
  return result;
}

BiomeModule BiomeModule::makeDomainWarpedNoise(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Domain Warped Noise",
                     ModuleType::DOMAIN_WARPED_NOISE,
                     LayerBlendMode::PLACE_SOLIDS);
  result.domainWarpedNoise = {};
  result.domainWarpedNoise.noise = makeDefaultVolumeNoiseSettings();
  result.domainWarpedNoise.noise.palette = makeStoneOrganicCrystalPalette();
  result.domainWarpedNoise.noise.baseScale = 0.030f;
  result.domainWarpedNoise.noise.octaves = 4;
  result.domainWarpedNoise.noise.threshold = 0.00f;
  result.domainWarpedNoise.warpOctaves = 2;
  result.domainWarpedNoise.warpPersistence = 0.5f;
  result.domainWarpedNoise.warpLacunarity = 2.0f;
  result.domainWarpedNoise.warpScale = 0.045f;
  result.domainWarpedNoise.warpStrength = 9.0f;
  return result;
}

BiomeModule BiomeModule::makeTreeGenerator(const std::string& id) {
  BiomeModule result =
      makeBaseModule(id, "Tree Generator", ModuleType::TREE_GENERATOR,
                     LayerBlendMode::PLACE_ON_AIR);
  result.treeGenerator = {};
  result.treeGenerator.spawnOnBlocks = {BlockType::MEMBRANE, BlockType::ORGANIC};
  result.treeGenerator.pattern = VoxPlacementPattern::RANDOM_SCATTER;
  result.treeGenerator.density = 0.35f;
  result.treeGenerator.treeType = TreeGeneratorType::NORMAL;
  result.treeGenerator.trunkBlock = BlockType::MEMBRANE_WEAVE;
  result.treeGenerator.leavesBlock = BlockType::ORGANIC;
  result.treeGenerator.infiniteY = false;
  result.treeGenerator.minY = -8;
  result.treeGenerator.maxY = 72;
  return result;
}

std::string sanitizePresetId(std::string value) {
  return sanitizeIdentifier(std::move(value), "biome_preset");
}

std::string sanitizeModuleId(std::string value) {
  return sanitizeIdentifier(std::move(value), "module");
}

PerlinDensityModule legacyDefaultPerlinDensity(int depth) {
  PerlinDensityModule module;
  module.baseScale = 0.04f + std::min(depth * 0.005f, 0.05f);
  module.densityThreshold = 0.28f;
  module.densityOctaves = std::min(3 + (depth / 2), 5);
  module.densityPersistence = 0.55f;
  module.warpOctaves = 2;
  module.warpStrength = 0.45f + std::min(depth * 0.10f, 0.75f);
  return module;
}

MaterialPaletteModule legacyDefaultMaterialPalette(int depth) {
  if (depth == 0) {
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::STONE,
            BlockType::STONE, BlockType::CRYSTAL, BlockType::VOID_MATTER};
  }
  if (depth == 1) {
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::MEMBRANE,
            BlockType::VOID_MATTER, BlockType::CRYSTAL, BlockType::STONE};
  }
  if (depth == 2) {
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::VOID_MATTER,
            BlockType::METAL, BlockType::CRYSTAL, BlockType::MEMBRANE};
  }

  switch ((depth - 3) % 4) {
  case 0:
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::STONE,
            BlockType::VOID_MATTER, BlockType::CRYSTAL, BlockType::STONE};
  case 1:
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::MEMBRANE,
            BlockType::ORGANIC, BlockType::CRYSTAL, BlockType::VOID_MATTER};
  case 2:
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::VOID_MATTER,
            BlockType::METAL, BlockType::CRYSTAL, BlockType::VOID_MATTER};
  default:
    return {BlockType::MEMBRANE, BlockType::ORGANIC, BlockType::ORGANIC,
            BlockType::MEMBRANE, BlockType::CRYSTAL, BlockType::VOID_MATTER};
  }
}

PerlinTerrainModule legacyDefaultPerlinTerrain(int depth) {
  PerlinTerrainModule module;
  module.density = legacyDefaultPerlinDensity(depth);
  module.palette = legacyDefaultMaterialPalette(depth);
  return module;
}

BiomePreset makeDefaultBiomePreset(const std::string& id, int depth) {
  BiomePreset preset;
  preset.id = sanitizePresetId(id);
  preset.displayName = "Default Perlin";
  preset.preview.depth = depth;
  preset.modules.push_back(BiomeModule::makePerlinTerrain("perlin", depth));
  return preset;
}

BiomeModule* findFirstModuleOfType(BiomePreset& preset, ModuleType type) {
  for (BiomeModule& module : preset.modules) {
    if (module.type == type) {
      return &module;
    }
  }
  return nullptr;
}

const BiomeModule* findFirstModuleOfType(const BiomePreset& preset,
                                         ModuleType type) {
  for (const BiomeModule& module : preset.modules) {
    if (module.type == type) {
      return &module;
    }
  }
  return nullptr;
}

PerlinDensityModule* findPerlinDensityModule(BiomePreset& preset) {
  BiomeModule* module = findFirstModuleOfType(preset, ModuleType::PERLIN_TERRAIN);
  return module ? &module->perlinTerrain.density : nullptr;
}

const PerlinDensityModule* findPerlinDensityModule(const BiomePreset& preset) {
  const BiomeModule* module =
      findFirstModuleOfType(preset, ModuleType::PERLIN_TERRAIN);
  return module ? &module->perlinTerrain.density : nullptr;
}

MaterialPaletteModule* findMaterialPaletteModule(BiomePreset& preset) {
  BiomeModule* module = findFirstModuleOfType(preset, ModuleType::PERLIN_TERRAIN);
  return module ? &module->perlinTerrain.palette : nullptr;
}

const MaterialPaletteModule* findMaterialPaletteModule(const BiomePreset& preset) {
  const BiomeModule* module =
      findFirstModuleOfType(preset, ModuleType::PERLIN_TERRAIN);
  return module ? &module->perlinTerrain.palette : nullptr;
}

} // namespace VoxelGame
