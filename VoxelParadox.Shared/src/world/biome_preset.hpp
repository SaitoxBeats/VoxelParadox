#pragma once

// biome_preset.hpp
// Unity mental model:
// - BiomePreset is a ScriptableObject-like asset for world generation.
// - BiomeModule is one generator layer inside that preset.
// - The helper functions below convert ids <-> enums, build defaults, and load/save
//   the preset JSON files used by the runtime and the biome editor.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "client_assets.hpp"
#include "block.hpp"
#include "vox_asset.hpp"

namespace VoxelGame {

// v3 stores every module inline in the biome preset JSON.
inline constexpr int kBiomePresetFormatVersion = 3;
inline constexpr int kBiomeModuleFormatVersion = 1;

// -----------------------------------------------------------------------------
// Enum ids used by JSON files and editor UI.
// -----------------------------------------------------------------------------

enum class PreviewMode : std::uint8_t {
  SANDBOX = 0,
  STREAMING = 1,
};

const char* previewModeId(PreviewMode mode);
const char* previewModeDisplayName(PreviewMode mode);
bool tryParsePreviewMode(const std::string& value, PreviewMode& outMode);

enum class LayerBlendMode : std::uint8_t {
  OVERWRITE_ALL = 0,
  PLACE_SOLIDS = 1,
  PLACE_ON_AIR = 2,
};

const char* layerBlendModeId(LayerBlendMode mode);
const char* layerBlendModeDisplayName(LayerBlendMode mode);
bool tryParseLayerBlendMode(const std::string& value, LayerBlendMode& outMode);

enum class ModuleType : std::uint8_t {
  PERLIN_TERRAIN = 0,
  IMPORT_VOX_FILES = 1,
  GRID_PATTERN = 2,
  MENGER_SPONGE = 3,
  CAVE_SYSTEM = 4,
  CELLULAR_NOISE = 5,
  FRACTAL_NOISE = 6,
  RIDGED_NOISE = 7,
  DOMAIN_WARPED_NOISE = 8,
  TREE_GENERATOR = 9,
};

const char* moduleTypeId(ModuleType type);
const char* moduleTypeDisplayName(ModuleType type);
bool tryParseModuleType(const std::string& value, ModuleType& outType);

enum class VoxPlacementPattern : std::uint8_t {
  RANDOM_SCATTER = 0,
  GRID = 1,
};

const char* voxPlacementPatternId(VoxPlacementPattern pattern);
const char* voxPlacementPatternDisplayName(VoxPlacementPattern pattern);
bool tryParseVoxPlacementPattern(const std::string& value,
                                 VoxPlacementPattern& outPattern);

enum class TreeGeneratorType : std::uint8_t {
  NORMAL = 0,
  STRANGE = 1,
  TRUNK_ONLY = 2,
};

const char* treeGeneratorTypeId(TreeGeneratorType type);
const char* treeGeneratorTypeDisplayName(TreeGeneratorType type);
bool tryParseTreeGeneratorType(const std::string& value,
                               TreeGeneratorType& outType);

enum class VoxRotationMode : std::uint8_t {
  FIXED = 0,
  RANDOM_90 = 1,
};

const char* voxRotationModeId(VoxRotationMode mode);
const char* voxRotationModeDisplayName(VoxRotationMode mode);
bool tryParseVoxRotationMode(const std::string& value, VoxRotationMode& outMode);

const char* voxColorMappingId(VoxColorMapping mapping);
const char* voxColorMappingDisplayName(VoxColorMapping mapping);
bool tryParseVoxColorMapping(const std::string& value, VoxColorMapping& outMapping);

// -----------------------------------------------------------------------------
// Shared preview and palette data.
// -----------------------------------------------------------------------------

struct PreviewSettings {
  std::uint32_t seed = 42;
  int depth = 0;
  PreviewMode defaultMode = PreviewMode::SANDBOX;
  glm::ivec3 sandboxRadius{2, 1, 2};
  int streamRenderDistance = 5;
};

struct PerlinDensityModule {
  float baseScale = 0.04f;
  float densityThreshold = 0.28f;
  int densityOctaves = 3;
  float densityPersistence = 0.55f;
  int warpOctaves = 2;
  float warpStrength = 0.45f;
};

struct MaterialPaletteModule {
  BlockType surfaceRib = BlockType::MEMBRANE;
  BlockType surfacePatch = BlockType::ORGANIC;
  BlockType shell = BlockType::STONE;
  BlockType core = BlockType::STONE;
  BlockType accent = BlockType::CRYSTAL;
  BlockType recess = BlockType::VOID_MATTER;
};

struct TricolorPaletteModule {
  BlockType primary = BlockType::STONE;
  BlockType secondary = BlockType::MEMBRANE;
  BlockType accent = BlockType::CRYSTAL;
};

struct VolumeNoiseModuleSettings {
  glm::ivec3 offset{0, 0, 0};
  bool infiniteY = false;
  int minY = -32;
  int maxY = 96;
  float baseScale = 0.035f;
  int octaves = 4;
  float persistence = 0.5f;
  float lacunarity = 2.0f;
  float threshold = 0.0f;
  bool invert = false;
  float secondaryNoiseScale = 0.04f;
  float secondaryThreshold = 0.18f;
  float accentNoiseScale = 0.08f;
  float accentThreshold = 0.62f;
  TricolorPaletteModule palette{};
};

// -----------------------------------------------------------------------------
// Module payloads. Only the payload matching BiomeModule::type is active.
// -----------------------------------------------------------------------------

struct PerlinTerrainModule {
  PerlinDensityModule density{};
  MaterialPaletteModule palette{};
};

struct ImportVoxFilesModule {
  std::string sourceDirectory = ClientAssets::kVoxDirectory;
  bool includeSubdirectories = true;
  VoxPlacementPattern pattern = VoxPlacementPattern::RANDOM_SCATTER;
  glm::ivec2 cellSize{96, 96};
  glm::ivec2 jitter{24, 24};
  bool infiniteY = false;
  int minY = -8;
  int maxY = 40;
  float spawnChance = 0.25f;
  VoxRotationMode rotationMode = VoxRotationMode::RANDOM_90;
  int fixedRotation = 0;
  BlockType defaultVoxel = BlockType::STONE;
  VoxColorMapping colorMapping = VoxColorMapping::DEFAULT;
  std::uint32_t seedOffset = 0;
};

struct GridPatternModule {
  glm::ivec3 cellSize{12, 12, 12};
  glm::ivec3 lineWidth{1, 1, 1};
  glm::ivec3 offset{0, 0, 0};
  int minAxisMatches = 2;
  int maxAxisMatches = 3;
  bool infiniteY = false;
  int minY = -32;
  int maxY = 96;
  float warpScale = 0.0f;
  float warpStrength = 0.0f;
  float accentNoiseScale = 0.08f;
  float accentThreshold = 0.68f;
  TricolorPaletteModule palette{};
};

struct MengerSpongeModule {
  glm::ivec3 cellSize{81, 81, 81};
  glm::ivec3 offset{0, 0, 0};
  int iterations = 4;
  int voidAxisThreshold = 2;
  bool invert = false;
  bool infiniteY = false;
  int minY = -32;
  int maxY = 96;
  float warpScale = 0.0f;
  float warpStrength = 0.0f;
  float accentNoiseScale = 0.06f;
  float accentThreshold = 0.72f;
  TricolorPaletteModule palette{};
};

struct CaveSystemModule {
  glm::ivec3 rootCellSize{96, 64, 96};
  glm::ivec3 offset{0, 0, 0};
  int recursionLevels = 4;
  int subdivisionFactor = 2;
  float tunnelRadius = 6.0f;
  float radiusFalloff = 0.58f;
  float chamberChance = 0.35f;
  float chamberRadiusScale = 1.8f;
  float centerJitter = 0.35f;
  bool infiniteY = false;
  int minY = -48;
  int maxY = 80;
  float warpScale = 0.03f;
  float warpStrength = 4.0f;
  float accentNoiseScale = 0.07f;
  float accentThreshold = 0.70f;
  TricolorPaletteModule palette{};
};

struct CellularNoiseModule {
  VolumeNoiseModuleSettings noise{};
  float jitter = 0.9f;
  float distanceBlend = 0.25f;
};

struct FractalNoiseModule {
  VolumeNoiseModuleSettings noise{};
};

struct RidgedNoiseModule {
  VolumeNoiseModuleSettings noise{};
  float ridgeSharpness = 1.5f;
};

struct DomainWarpedNoiseModule {
  VolumeNoiseModuleSettings noise{};
  int warpOctaves = 2;
  float warpPersistence = 0.5f;
  float warpLacunarity = 2.0f;
  float warpScale = 0.045f;
  float warpStrength = 10.0f;
};

struct TreeGeneratorModule {
  std::vector<BlockType> spawnOnBlocks{BlockType::MEMBRANE, BlockType::ORGANIC};
  VoxPlacementPattern pattern = VoxPlacementPattern::RANDOM_SCATTER;
  float density = 0.35f;
  TreeGeneratorType treeType = TreeGeneratorType::NORMAL;
  BlockType trunkBlock = BlockType::MEMBRANE_WEAVE;
  BlockType leavesBlock = BlockType::ORGANIC;
  bool infiniteY = false;
  int minY = -8;
  int maxY = 72;
};

// -----------------------------------------------------------------------------
// Public asset types used by runtime and editor.
// -----------------------------------------------------------------------------

struct BiomeModule {
  int formatVersion = kBiomeModuleFormatVersion;
  std::string id = "perlin";
  std::string displayName = "Perlin";
  std::filesystem::path filePath{};
  ModuleType type = ModuleType::PERLIN_TERRAIN;
  bool enabled = true;
  LayerBlendMode blendMode = LayerBlendMode::OVERWRITE_ALL;

  PerlinTerrainModule perlinTerrain{};
  ImportVoxFilesModule importVoxFiles{};
  GridPatternModule gridPattern{};
  MengerSpongeModule mengerSponge{};
  CaveSystemModule caveSystem{};
  CellularNoiseModule cellularNoise{};
  FractalNoiseModule fractalNoise{};
  RidgedNoiseModule ridgedNoise{};
  DomainWarpedNoiseModule domainWarpedNoise{};
  TreeGeneratorModule treeGenerator{};

  static BiomeModule makePerlinTerrain(const std::string& id = "perlin",
                                       int depth = 0);
  static BiomeModule makeImportVoxFiles(const std::string& id = "vox_import");
  static BiomeModule makeGridPattern(const std::string& id = "grid_pattern");
  static BiomeModule makeMengerSponge(const std::string& id = "menger_sponge");
  static BiomeModule makeCaveSystem(const std::string& id = "cave_system");
  static BiomeModule makeCellularNoise(
      const std::string& id = "cellular_noise");
  static BiomeModule makeFractalNoise(const std::string& id = "fractal_noise");
  static BiomeModule makeRidgedNoise(const std::string& id = "ridged_noise");
  static BiomeModule makeDomainWarpedNoise(
      const std::string& id = "domain_warped_noise");
  static BiomeModule makeTreeGenerator(
      const std::string& id = "tree_generator");
};

struct BiomePreset {
  int formatVersion = kBiomePresetFormatVersion;
  std::string id = "default_perlin";
  std::string displayName = "Default Perlin";
  PreviewSettings preview{};
  std::vector<BiomeModule> modules;
};

// -----------------------------------------------------------------------------
// Defaults and lookups used by runtime/editor code.
// -----------------------------------------------------------------------------

std::string sanitizePresetId(std::string value);
std::string sanitizeModuleId(std::string value);

// Legacy defaults are still used to build sane starting presets and to migrate
// old files forward into the current module format.
PerlinDensityModule legacyDefaultPerlinDensity(int depth);
MaterialPaletteModule legacyDefaultMaterialPalette(int depth);
PerlinTerrainModule legacyDefaultPerlinTerrain(int depth);
BiomePreset makeDefaultBiomePreset(const std::string& id = "default_perlin",
                                   int depth = 0);

BiomeModule* findFirstModuleOfType(BiomePreset& preset, ModuleType type);
const BiomeModule* findFirstModuleOfType(const BiomePreset& preset,
                                         ModuleType type);
PerlinDensityModule* findPerlinDensityModule(BiomePreset& preset);
const PerlinDensityModule* findPerlinDensityModule(const BiomePreset& preset);
MaterialPaletteModule* findMaterialPaletteModule(BiomePreset& preset);
const MaterialPaletteModule* findMaterialPaletteModule(const BiomePreset& preset);

// -----------------------------------------------------------------------------
// Persistence API used by the client runtime and the BiomeMaker tool.
// -----------------------------------------------------------------------------

std::filesystem::path suggestModulePath(const BiomePreset& preset,
                                        const BiomeModule& module);
bool loadBiomeModuleFromFile(const std::filesystem::path& path, BiomeModule& outModule,
                             std::string& outError);
bool saveBiomeModuleToFile(const std::filesystem::path& path,
                           const BiomeModule& module, std::string& outError);
bool loadBiomePresetFromFile(const std::filesystem::path& path, BiomePreset& outPreset,
                             std::string& outError);
bool saveBiomePresetToFile(const std::filesystem::path& path, const BiomePreset& preset,
                           std::string& outError);

} // namespace VoxelGame
