// biome_preset.cpp
// Unity mental model: this is the serialization/persistence layer for biome
// presets. It translates between JSON files and the in-memory preset structs.

#pragma region Includes
#include "world/biome/biome_preset_internal.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>
#pragma endregion

#pragma region BiomePresetImplementation
namespace VoxelGame {
namespace {

using json = nlohmann::json;

// Funcao: executa 'clampSandboxRadius' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'radius' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
glm::ivec3 clampSandboxRadius(glm::ivec3 radius) {
  radius.x = std::max(radius.x, 0);
  radius.y = std::max(radius.y, 0);
  radius.z = std::max(radius.z, 0);
  return radius;
}

// Funcao: executa 'clampPositiveIvec2' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'minValue' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec2' com o resultado composto por esta chamada.
glm::ivec2 clampPositiveIvec2(glm::ivec2 value, int minValue) {
  value.x = std::max(value.x, minValue);
  value.y = std::max(value.y, minValue);
  return value;
}

// Funcao: executa 'clampPositiveIvec3' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'minValue' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
glm::ivec3 clampPositiveIvec3(glm::ivec3 value, int minValue) {
  value.x = std::max(value.x, minValue);
  value.y = std::max(value.y, minValue);
  value.z = std::max(value.z, minValue);
  return value;
}

// Funcao: executa 'clampStreamRenderDistance' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'renderDistance' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
int clampStreamRenderDistance(int renderDistance) {
  return std::clamp(renderDistance, 2, 10);
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const glm::ivec3& value) {
  return json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outValue' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, glm::ivec3& outValue) {
  if (!value.is_object()) {
    return false;
  }

  outValue.x = value.value("x", 0);
  outValue.y = value.value("y", 0);
  outValue.z = value.value("z", 0);
  return true;
}

// Funcao: executa 'toJsonXZ' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJsonXZ(const glm::ivec2& value) {
  return json{{"x", value.x}, {"z", value.y}};
}

// Funcao: executa 'fromJsonXZ' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outValue' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJsonXZ(const json& value, glm::ivec2& outValue) {
  if (!value.is_object()) {
    return false;
  }

  outValue.x = value.value("x", 0);
  outValue.y = value.value("z", 0);
  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'preview' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const PreviewSettings& preview) {
  return json{
      {"seed", preview.seed},
      {"depth", preview.depth},
      {"default_mode", previewModeId(preview.defaultMode)},
      {"sandbox_radius", toJson(preview.sandboxRadius)},
      {"stream_render_distance", preview.streamRenderDistance},
  };
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outPreview', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, PreviewSettings& outPreview,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "Preview settings must be an object.";
    return false;
  }

  outPreview.seed = value.value("seed", 42u);
  outPreview.depth = value.value("depth", 0);

  PreviewMode parsedMode = PreviewMode::SANDBOX;
  if (!tryParsePreviewMode(value.value("default_mode", std::string("sandbox")),
                           parsedMode)) {
    outError = "Unsupported preview.default_mode value.";
    return false;
  }
  outPreview.defaultMode = parsedMode;

  glm::ivec3 radius{2, 1, 2};
  if (!fromJson(value.value("sandbox_radius", json::object()), radius)) {
    outError = "preview.sandbox_radius must be an object with x/y/z.";
    return false;
  }
  outPreview.sandboxRadius = clampSandboxRadius(radius);
  outPreview.streamRenderDistance =
      clampStreamRenderDistance(value.value("stream_render_distance", 5));
  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'blockType' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(BlockId blockType) {
  return getBlockId(blockType);
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outBlockType' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, BlockId& outBlockType) {
  if (!value.is_string()) {
    return false;
  }
  return tryParseBlockType(value.get<std::string>(), outBlockType);
}

json toJson(const std::vector<BlockId>& blockTypes) {
  json result = json::array();
  for (BlockId blockType : blockTypes) {
    if (blockType == BlockIds::AIR || blockType == BlockIds::COUNT) {
      continue;
    }
    result.push_back(toJson(blockType));
  }
  return result;
}

bool fromJson(const json& value, std::vector<BlockId>& outBlockTypes,
              std::string& outError, const char* contextName) {
  if (!value.is_array()) {
    outError = std::string(contextName) + " must be an array of block ids.";
    return false;
  }

  std::vector<BlockId> blockTypes;
  blockTypes.reserve(value.size());
  for (const json& blockValue : value) {
    BlockId blockType = BlockIds::AIR;
    if (!fromJson(blockValue, blockType) || blockType == BlockIds::AIR ||
        blockType == BlockIds::COUNT) {
      outError = std::string(contextName) + " uses an unknown block id.";
      return false;
    }
    if (std::find(blockTypes.begin(), blockTypes.end(), blockType) ==
        blockTypes.end()) {
      blockTypes.push_back(blockType);
    }
  }

  if (blockTypes.empty()) {
    outError = std::string(contextName) + " must contain at least one block.";
    return false;
  }

  outBlockTypes = std::move(blockTypes);
  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'palette' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const TricolorPaletteModule& palette) {
  return json{
      {"primary", toJson(palette.primary)},
      {"secondary", toJson(palette.secondary)},
      {"accent", toJson(palette.accent)},
  };
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outPalette', 'outError', 'contextName' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, TricolorPaletteModule& outPalette,
              std::string& outError, const char* contextName) {
  if (!value.is_object()) {
    outError = std::string(contextName) + " palette settings must be an object.";
    return false;
  }

  if (!fromJson(value.value("primary", json()), outPalette.primary) ||
      !fromJson(value.value("secondary", json()), outPalette.secondary) ||
      !fromJson(value.value("accent", json()), outPalette.accent)) {
    outError = std::string(contextName) + " palette uses unknown block ids.";
    return false;
  }

  return true;
}

json toJson(const VolumeNoiseModuleSettings& settings) {
  json root = toJson(settings.palette);
  root["offset"] = toJson(settings.offset);
  root["infinite_y"] = settings.infiniteY;
  root["min_y"] = settings.minY;
  root["max_y"] = settings.maxY;
  root["base_scale"] = settings.baseScale;
  root["octaves"] = settings.octaves;
  root["persistence"] = settings.persistence;
  root["lacunarity"] = settings.lacunarity;
  root["threshold"] = settings.threshold;
  root["invert"] = settings.invert;
  root["secondary_noise_scale"] = settings.secondaryNoiseScale;
  root["secondary_threshold"] = settings.secondaryThreshold;
  root["accent_noise_scale"] = settings.accentNoiseScale;
  root["accent_threshold"] = settings.accentThreshold;
  return root;
}

bool fromJson(const json& value, VolumeNoiseModuleSettings& outSettings,
              std::string& outError, const char* contextName) {
  if (!value.is_object()) {
    outError = std::string(contextName) + ".settings must be an object.";
    return false;
  }

  glm::ivec3 offset = outSettings.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError =
        std::string(contextName) + ".settings.offset must be an object with x/y/z.";
    return false;
  }
  outSettings.offset = offset;

  outSettings.infiniteY = value.value("infinite_y", outSettings.infiniteY);
  outSettings.minY = value.value("min_y", outSettings.minY);
  outSettings.maxY = value.value("max_y", outSettings.maxY);
  if (outSettings.maxY < outSettings.minY) {
    std::swap(outSettings.minY, outSettings.maxY);
  }

  outSettings.baseScale =
      std::max(0.0001f, value.value("base_scale", outSettings.baseScale));
  outSettings.octaves =
      std::clamp(value.value("octaves", outSettings.octaves), 1, 8);
  outSettings.persistence =
      std::clamp(value.value("persistence", outSettings.persistence), 0.05f, 0.99f);
  outSettings.lacunarity =
      std::clamp(value.value("lacunarity", outSettings.lacunarity), 1.05f, 6.0f);
  outSettings.threshold =
      std::clamp(value.value("threshold", outSettings.threshold), -1.0f, 1.0f);
  outSettings.invert = value.value("invert", outSettings.invert);
  outSettings.secondaryNoiseScale = std::max(
      0.0f, value.value("secondary_noise_scale", outSettings.secondaryNoiseScale));
  outSettings.secondaryThreshold = std::clamp(
      value.value("secondary_threshold", outSettings.secondaryThreshold), -1.0f, 1.0f);
  outSettings.accentNoiseScale = std::max(
      0.0f, value.value("accent_noise_scale", outSettings.accentNoiseScale));
  outSettings.accentThreshold = std::clamp(
      value.value("accent_threshold", outSettings.accentThreshold), -1.0f, 1.0f);

  return fromJson(value, outSettings.palette, outError, contextName);
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const PerlinTerrainModule& module) {
  return json{
      {"base_scale", module.density.baseScale},
      {"density_threshold", module.density.densityThreshold},
      {"density_octaves", module.density.densityOctaves},
      {"density_persistence", module.density.densityPersistence},
      {"warp_octaves", module.density.warpOctaves},
      {"warp_strength", module.density.warpStrength},
      {"surface_rib", toJson(module.palette.surfaceRib)},
      {"surface_patch", toJson(module.palette.surfacePatch)},
      {"shell", toJson(module.palette.shell)},
      {"core", toJson(module.palette.core)},
      {"accent", toJson(module.palette.accent)},
      {"recess", toJson(module.palette.recess)},
  };
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, PerlinTerrainModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "perlin_terrain.settings must be an object.";
    return false;
  }

  outModule.density.baseScale = value.value("base_scale", 0.04f);
  outModule.density.densityThreshold = value.value("density_threshold", 0.28f);
  outModule.density.densityOctaves = std::max(1, value.value("density_octaves", 3));
  outModule.density.densityPersistence = value.value("density_persistence", 0.55f);
  outModule.density.warpOctaves = std::max(1, value.value("warp_octaves", 2));
  outModule.density.warpStrength = value.value("warp_strength", 0.45f);

  if (!fromJson(value.value("surface_rib", json()), outModule.palette.surfaceRib) ||
      !fromJson(value.value("surface_patch", json()),
                outModule.palette.surfacePatch) ||
      !fromJson(value.value("shell", json()), outModule.palette.shell) ||
      !fromJson(value.value("core", json()), outModule.palette.core) ||
      !fromJson(value.value("accent", json()), outModule.palette.accent) ||
      !fromJson(value.value("recess", json()), outModule.palette.recess)) {
    outError = "perlin_terrain.settings uses unknown block ids.";
    return false;
  }

  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const ImportVoxFilesModule& module) {
  return json{
      {"source_directory", module.sourceDirectory},
      {"include_subdirectories", module.includeSubdirectories},
      {"pattern", voxPlacementPatternId(module.pattern)},
      {"cell_size", toJsonXZ(module.cellSize)},
      {"jitter", toJsonXZ(module.jitter)},
      {"infinite_y", module.infiniteY},
      {"min_y", module.minY},
      {"max_y", module.maxY},
      {"spawn_chance", module.spawnChance},
      {"rotation_mode", voxRotationModeId(module.rotationMode)},
      {"fixed_rotation", module.fixedRotation},
      {"default_voxel", toJson(module.defaultVoxel)},
      {"color_mapping", voxColorMappingId(module.colorMapping)},
      {"seed_offset", module.seedOffset},
  };
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, ImportVoxFilesModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "import_vox_files.settings must be an object.";
    return false;
  }

  outModule.sourceDirectory =
      value.value("source_directory", std::string("Assets/Voxs"));
  outModule.includeSubdirectories = value.value("include_subdirectories", true);

  VoxPlacementPattern pattern = VoxPlacementPattern::RANDOM_SCATTER;
  if (!tryParseVoxPlacementPattern(value.value("pattern", std::string("random_scatter")),
                                   pattern)) {
    outError = "Unsupported import_vox_files.settings.pattern value.";
    return false;
  }
  outModule.pattern = pattern;

  glm::ivec2 cellSize = outModule.cellSize;
  if (!fromJsonXZ(value.value("cell_size", json::object()), cellSize)) {
    outError = "import_vox_files.settings.cell_size must be an object with x/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec2(cellSize, 1);

  glm::ivec2 jitter = outModule.jitter;
  if (!fromJsonXZ(value.value("jitter", json::object()), jitter)) {
    outError = "import_vox_files.settings.jitter must be an object with x/z.";
    return false;
  }
  outModule.jitter = glm::max(jitter, glm::ivec2(0));

  outModule.infiniteY = value.value("infinite_y", false);
  outModule.minY = value.value("min_y", -8);
  outModule.maxY = value.value("max_y", 40);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }

  outModule.spawnChance =
      std::clamp(value.value("spawn_chance", 0.25f), 0.0f, 1.0f);

  VoxRotationMode rotationMode = VoxRotationMode::RANDOM_90;
  if (!tryParseVoxRotationMode(
          value.value("rotation_mode", std::string("random_90")), rotationMode)) {
    outError = "Unsupported import_vox_files.settings.rotation_mode value.";
    return false;
  }
  outModule.rotationMode = rotationMode;
  outModule.fixedRotation = std::clamp(value.value("fixed_rotation", 0), 0, 3);

  BlockId defaultVoxel = outModule.defaultVoxel;
  if (!fromJson(value.value("default_voxel", json()), defaultVoxel)) {
    outError = "import_vox_files.settings.default_voxel uses an unknown block id.";
    return false;
  }
  outModule.defaultVoxel = defaultVoxel;

  VoxColorMapping colorMapping = VoxColorMapping::DEFAULT;
  if (!tryParseVoxColorMapping(
          value.value("color_mapping", std::string("default")), colorMapping)) {
    outError = "Unsupported import_vox_files.settings.color_mapping value.";
    return false;
  }
  outModule.colorMapping = colorMapping;
  outModule.seedOffset = value.value("seed_offset", 0u);
  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const GridPatternModule& module) {
  json root = toJson(module.palette);
  root["cell_size"] = toJson(module.cellSize);
  root["line_width"] = toJson(module.lineWidth);
  root["offset"] = toJson(module.offset);
  root["min_axis_matches"] = module.minAxisMatches;
  root["max_axis_matches"] = module.maxAxisMatches;
  root["infinite_y"] = module.infiniteY;
  root["min_y"] = module.minY;
  root["max_y"] = module.maxY;
  root["warp_scale"] = module.warpScale;
  root["warp_strength"] = module.warpStrength;
  root["accent_noise_scale"] = module.accentNoiseScale;
  root["accent_threshold"] = module.accentThreshold;
  return root;
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, GridPatternModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "grid_pattern.settings must be an object.";
    return false;
  }

  glm::ivec3 cellSize = outModule.cellSize;
  if (!fromJson(value.value("cell_size", json::object()), cellSize)) {
    outError = "grid_pattern.settings.cell_size must be an object with x/y/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec3(cellSize, 1);

  glm::ivec3 lineWidth = outModule.lineWidth;
  if (!fromJson(value.value("line_width", json::object()), lineWidth)) {
    outError = "grid_pattern.settings.line_width must be an object with x/y/z.";
    return false;
  }
  outModule.lineWidth = glm::min(clampPositiveIvec3(lineWidth, 1), outModule.cellSize);

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "grid_pattern.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.minAxisMatches =
      std::clamp(value.value("min_axis_matches", outModule.minAxisMatches), 1, 3);
  outModule.maxAxisMatches =
      std::clamp(value.value("max_axis_matches", outModule.maxAxisMatches),
                 outModule.minAxisMatches, 3);
  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }
  outModule.warpScale = std::max(0.0f, value.value("warp_scale", outModule.warpScale));
  outModule.warpStrength =
      std::max(0.0f, value.value("warp_strength", outModule.warpStrength));
  outModule.accentNoiseScale =
      std::max(0.0f, value.value("accent_noise_scale", outModule.accentNoiseScale));
  outModule.accentThreshold =
      std::clamp(value.value("accent_threshold", outModule.accentThreshold), -1.0f,
                 1.0f);

  return fromJson(value, outModule.palette, outError, "grid_pattern");
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const MengerSpongeModule& module) {
  json root = toJson(module.palette);
  root["cell_size"] = toJson(module.cellSize);
  root["offset"] = toJson(module.offset);
  root["iterations"] = module.iterations;
  root["void_axis_threshold"] = module.voidAxisThreshold;
  root["invert"] = module.invert;
  root["infinite_y"] = module.infiniteY;
  root["min_y"] = module.minY;
  root["max_y"] = module.maxY;
  root["warp_scale"] = module.warpScale;
  root["warp_strength"] = module.warpStrength;
  root["accent_noise_scale"] = module.accentNoiseScale;
  root["accent_threshold"] = module.accentThreshold;
  return root;
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, MengerSpongeModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "menger_sponge.settings must be an object.";
    return false;
  }

  glm::ivec3 cellSize = outModule.cellSize;
  if (!fromJson(value.value("cell_size", json::object()), cellSize)) {
    outError = "menger_sponge.settings.cell_size must be an object with x/y/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec3(cellSize, 3);

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "menger_sponge.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.iterations =
      std::clamp(value.value("iterations", outModule.iterations), 1, 7);
  outModule.voidAxisThreshold =
      std::clamp(value.value("void_axis_threshold", outModule.voidAxisThreshold), 1,
                 3);
  outModule.invert = value.value("invert", outModule.invert);
  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }
  outModule.warpScale = std::max(0.0f, value.value("warp_scale", outModule.warpScale));
  outModule.warpStrength =
      std::max(0.0f, value.value("warp_strength", outModule.warpStrength));
  outModule.accentNoiseScale =
      std::max(0.0f, value.value("accent_noise_scale", outModule.accentNoiseScale));
  outModule.accentThreshold =
      std::clamp(value.value("accent_threshold", outModule.accentThreshold), -1.0f,
                 1.0f);

  return fromJson(value, outModule.palette, outError, "menger_sponge");
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const CaveSystemModule& module) {
  json root = toJson(module.palette);
  root["root_cell_size"] = toJson(module.rootCellSize);
  root["offset"] = toJson(module.offset);
  root["recursion_levels"] = module.recursionLevels;
  root["subdivision_factor"] = module.subdivisionFactor;
  root["tunnel_radius"] = module.tunnelRadius;
  root["radius_falloff"] = module.radiusFalloff;
  root["chamber_chance"] = module.chamberChance;
  root["chamber_radius_scale"] = module.chamberRadiusScale;
  root["center_jitter"] = module.centerJitter;
  root["infinite_y"] = module.infiniteY;
  root["min_y"] = module.minY;
  root["max_y"] = module.maxY;
  root["warp_scale"] = module.warpScale;
  root["warp_strength"] = module.warpStrength;
  root["accent_noise_scale"] = module.accentNoiseScale;
  root["accent_threshold"] = module.accentThreshold;
  return root;
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, CaveSystemModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "cave_system.settings must be an object.";
    return false;
  }

  glm::ivec3 rootCellSize = outModule.rootCellSize;
  if (!fromJson(value.value("root_cell_size", json::object()), rootCellSize)) {
    outError = "cave_system.settings.root_cell_size must be an object with x/y/z.";
    return false;
  }
  outModule.rootCellSize = clampPositiveIvec3(rootCellSize, 4);

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "cave_system.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.recursionLevels =
      std::clamp(value.value("recursion_levels", outModule.recursionLevels), 1, 7);
  outModule.subdivisionFactor =
      std::clamp(value.value("subdivision_factor", outModule.subdivisionFactor), 2,
                 4);
  outModule.tunnelRadius =
      std::clamp(value.value("tunnel_radius", outModule.tunnelRadius), 0.5f, 64.0f);
  outModule.radiusFalloff =
      std::clamp(value.value("radius_falloff", outModule.radiusFalloff), 0.15f,
                 0.95f);
  outModule.chamberChance =
      std::clamp(value.value("chamber_chance", outModule.chamberChance), 0.0f, 1.0f);
  outModule.chamberRadiusScale =
      std::clamp(value.value("chamber_radius_scale", outModule.chamberRadiusScale),
                 0.5f, 4.0f);
  outModule.centerJitter =
      std::clamp(value.value("center_jitter", outModule.centerJitter), 0.0f, 0.45f);
  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }
  outModule.warpScale = std::max(0.0f, value.value("warp_scale", outModule.warpScale));
  outModule.warpStrength =
      std::max(0.0f, value.value("warp_strength", outModule.warpStrength));
  outModule.accentNoiseScale =
      std::max(0.0f, value.value("accent_noise_scale", outModule.accentNoiseScale));
  outModule.accentThreshold =
      std::clamp(value.value("accent_threshold", outModule.accentThreshold), -1.0f,
                 1.0f);

  return fromJson(value, outModule.palette, outError, "cave_system");
}

json toJson(const CellularNoiseModule& module) {
  json root = toJson(module.noise);
  root["jitter"] = module.jitter;
  root["distance_blend"] = module.distanceBlend;
  return root;
}

bool fromJson(const json& value, CellularNoiseModule& outModule,
              std::string& outError) {
  if (!fromJson(value, outModule.noise, outError, "cellular_noise")) {
    return false;
  }

  outModule.jitter = std::clamp(value.value("jitter", outModule.jitter), 0.0f, 1.0f);
  outModule.distanceBlend = std::clamp(
      value.value("distance_blend", outModule.distanceBlend), 0.0f, 1.0f);
  return true;
}

json toJson(const FractalNoiseModule& module) { return toJson(module.noise); }

bool fromJson(const json& value, FractalNoiseModule& outModule,
              std::string& outError) {
  return fromJson(value, outModule.noise, outError, "fractal_noise");
}

json toJson(const RidgedNoiseModule& module) {
  json root = toJson(module.noise);
  root["ridge_sharpness"] = module.ridgeSharpness;
  return root;
}

bool fromJson(const json& value, RidgedNoiseModule& outModule,
              std::string& outError) {
  if (!fromJson(value, outModule.noise, outError, "ridged_noise")) {
    return false;
  }

  outModule.ridgeSharpness = std::clamp(
      value.value("ridge_sharpness", outModule.ridgeSharpness), 0.05f, 8.0f);
  return true;
}

json toJson(const DomainWarpedNoiseModule& module) {
  json root = toJson(module.noise);
  root["warp_octaves"] = module.warpOctaves;
  root["warp_persistence"] = module.warpPersistence;
  root["warp_lacunarity"] = module.warpLacunarity;
  root["warp_scale"] = module.warpScale;
  root["warp_strength"] = module.warpStrength;
  return root;
}

bool fromJson(const json& value, DomainWarpedNoiseModule& outModule,
              std::string& outError) {
  if (!fromJson(value, outModule.noise, outError, "domain_warped_noise")) {
    return false;
  }

  outModule.warpOctaves =
      std::clamp(value.value("warp_octaves", outModule.warpOctaves), 1, 6);
  outModule.warpPersistence = std::clamp(
      value.value("warp_persistence", outModule.warpPersistence), 0.05f, 0.99f);
  outModule.warpLacunarity = std::clamp(
      value.value("warp_lacunarity", outModule.warpLacunarity), 1.05f, 6.0f);
  outModule.warpScale =
      std::max(0.0f, value.value("warp_scale", outModule.warpScale));
  outModule.warpStrength =
      std::max(0.0f, value.value("warp_strength", outModule.warpStrength));
  return true;
}

json toJson(const TreeGeneratorModule& module) {
  return json{
      {"spawn_on_blocks", toJson(module.spawnOnBlocks)},
      {"pattern", voxPlacementPatternId(module.pattern)},
      {"density", module.density},
      {"tree_type", treeGeneratorTypeId(module.treeType)},
      {"trunk_block", toJson(module.trunkBlock)},
      {"leaves_block", toJson(module.leavesBlock)},
      {"infinite_y", module.infiniteY},
      {"min_y", module.minY},
      {"max_y", module.maxY},
  };
}

bool fromJson(const json& value, TreeGeneratorModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "tree_generator.settings must be an object.";
    return false;
  }

  if (!fromJson(value.value("spawn_on_blocks", json::array()),
                outModule.spawnOnBlocks, outError,
                "tree_generator.settings.spawn_on_blocks")) {
    return false;
  }

  VoxPlacementPattern pattern = outModule.pattern;
  if (!tryParseVoxPlacementPattern(
          value.value("pattern", std::string(voxPlacementPatternId(outModule.pattern))),
          pattern)) {
    outError = "Unsupported tree_generator.settings.pattern value.";
    return false;
  }
  outModule.pattern = pattern;

  outModule.density =
      std::clamp(value.value("density", outModule.density), 0.0f, 1.0f);

  TreeGeneratorType treeType = outModule.treeType;
  if (!tryParseTreeGeneratorType(
          value.value("tree_type",
                      std::string(treeGeneratorTypeId(outModule.treeType))),
          treeType)) {
    outError = "Unsupported tree_generator.settings.tree_type value.";
    return false;
  }
  outModule.treeType = treeType;

  BlockId trunkBlock = outModule.trunkBlock;
  if (!fromJson(value.value("trunk_block", json()), trunkBlock) ||
      trunkBlock == BlockIds::AIR || trunkBlock == BlockIds::COUNT) {
    outError = "tree_generator.settings.trunk_block uses an unknown block id.";
    return false;
  }
  outModule.trunkBlock = trunkBlock;

  BlockId leavesBlock = outModule.leavesBlock;
  if (!fromJson(value.value("leaves_block", json()), leavesBlock) ||
      leavesBlock == BlockIds::AIR || leavesBlock == BlockIds::COUNT) {
    outError = "tree_generator.settings.leaves_block uses an unknown block id.";
    return false;
  }
  outModule.leavesBlock = leavesBlock;

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }

  return true;
}

json toJson(const FloatingIslandsModule& module) {
  json root = toJson(module.palette);
  root["offset"] = toJson(module.offset);
  root["infinite_y"] = module.infiniteY;
  root["min_y"] = module.minY;
  root["max_y"] = module.maxY;
  root["cell_size"] = toJsonXZ(module.cellSize);
  root["jitter"] = toJsonXZ(module.jitter);
  root["vertical_spacing"] = module.verticalSpacing;
  root["vertical_jitter"] = module.verticalJitter;
  root["spawn_chance"] = module.spawnChance;
  root["min_radius"] = module.minRadius;
  root["max_radius"] = module.maxRadius;
  root["min_height"] = module.minHeight;
  root["max_height"] = module.maxHeight;
  root["surface_thickness"] = module.surfaceThickness;
  root["underside_steepness"] = module.undersideSteepness;
  root["edge_noise_scale"] = module.edgeNoiseScale;
  root["edge_noise_strength"] = module.edgeNoiseStrength;
  root["accent_noise_scale"] = module.accentNoiseScale;
  root["accent_threshold"] = module.accentThreshold;
  return root;
}

bool fromJson(const json& value, FloatingIslandsModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "floating_islands.settings must be an object.";
    return false;
  }

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "floating_islands.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }

  glm::ivec2 cellSize = outModule.cellSize;
  if (!fromJsonXZ(value.value("cell_size", json::object()), cellSize)) {
    outError = "floating_islands.settings.cell_size must be an object with x/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec2(cellSize, 8);

  glm::ivec2 jitter = outModule.jitter;
  if (!fromJsonXZ(value.value("jitter", json::object()), jitter)) {
    outError = "floating_islands.settings.jitter must be an object with x/z.";
    return false;
  }
  outModule.jitter = glm::max(jitter, glm::ivec2(0));

  outModule.verticalSpacing =
      std::max(8, value.value("vertical_spacing", outModule.verticalSpacing));
  outModule.verticalJitter =
      std::max(0, value.value("vertical_jitter", outModule.verticalJitter));
  outModule.spawnChance =
      std::clamp(value.value("spawn_chance", outModule.spawnChance), 0.0f, 1.0f);
  outModule.minRadius =
      std::max(1, value.value("min_radius", outModule.minRadius));
  outModule.maxRadius =
      std::max(outModule.minRadius,
               value.value("max_radius", outModule.maxRadius));
  outModule.minHeight =
      std::max(1, value.value("min_height", outModule.minHeight));
  outModule.maxHeight =
      std::max(outModule.minHeight,
               value.value("max_height", outModule.maxHeight));
  outModule.surfaceThickness =
      std::max(0.0f, value.value("surface_thickness", outModule.surfaceThickness));
  outModule.undersideSteepness =
      std::clamp(value.value("underside_steepness",
                             outModule.undersideSteepness),
                 0.20f, 4.0f);
  outModule.edgeNoiseScale =
      std::max(0.0f, value.value("edge_noise_scale", outModule.edgeNoiseScale));
  outModule.edgeNoiseStrength =
      std::clamp(value.value("edge_noise_strength",
                             outModule.edgeNoiseStrength),
                 0.0f, 1.0f);
  outModule.accentNoiseScale =
      std::max(0.0f,
               value.value("accent_noise_scale", outModule.accentNoiseScale));
  outModule.accentThreshold =
      std::clamp(value.value("accent_threshold", outModule.accentThreshold),
                 -1.0f, 1.0f);

  return fromJson(value, outModule.palette, outError, "floating_islands");
}

json toJson(const OneBlockModule& module) {
  return json{
      {"support_block", toJson(module.supportBlock)},
      {"block", toJson(module.block)},
      {"accent_block", toJson(module.accentBlock)},
      {"pattern", voxPlacementPatternId(module.pattern)},
      {"density", module.density},
      {"cell_size", toJsonXZ(module.cellSize)},
      {"jitter", toJsonXZ(module.jitter)},
      {"infinite_y", module.infiniteY},
      {"min_y", module.minY},
      {"max_y", module.maxY},
      {"min_blocks", module.minBlocks},
      {"max_blocks", module.maxBlocks},
      {"accent_chance", module.accentChance},
      {"require_air", module.requireAir},
  };
}

bool fromJson(const json& value, OneBlockModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "one_block.settings must be an object.";
    return false;
  }

  BlockId supportBlock = outModule.supportBlock;
  if (!fromJson(value.value("support_block", json()), supportBlock) ||
      supportBlock == BlockIds::AIR || supportBlock == BlockIds::COUNT) {
    outError = "one_block.settings.support_block uses an unknown block id.";
    return false;
  }
  outModule.supportBlock = supportBlock;

  BlockId block = outModule.block;
  if (!fromJson(value.value("block", json()), block) ||
      block == BlockIds::AIR || block == BlockIds::COUNT) {
    outError = "one_block.settings.block uses an unknown block id.";
    return false;
  }
  outModule.block = block;

  BlockId accentBlock = outModule.accentBlock;
  if (!fromJson(value.value("accent_block", json()), accentBlock) ||
      accentBlock == BlockIds::AIR || accentBlock == BlockIds::COUNT) {
    outError = "one_block.settings.accent_block uses an unknown block id.";
    return false;
  }
  outModule.accentBlock = accentBlock;

  VoxPlacementPattern pattern = outModule.pattern;
  if (!tryParseVoxPlacementPattern(
          value.value("pattern",
                      std::string(voxPlacementPatternId(outModule.pattern))),
          pattern)) {
    outError = "Unsupported one_block.settings.pattern value.";
    return false;
  }
  outModule.pattern = pattern;
  outModule.density =
      std::clamp(value.value("density", outModule.density), 0.0f, 1.0f);

  glm::ivec2 cellSize = outModule.cellSize;
  if (!fromJsonXZ(value.value("cell_size", json::object()), cellSize)) {
    outError = "one_block.settings.cell_size must be an object with x/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec2(cellSize, 1);

  glm::ivec2 jitter = outModule.jitter;
  if (!fromJsonXZ(value.value("jitter", json::object()), jitter)) {
    outError = "one_block.settings.jitter must be an object with x/z.";
    return false;
  }
  outModule.jitter = glm::max(jitter, glm::ivec2(0));

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }
  outModule.minBlocks =
      std::max(1, value.value("min_blocks", outModule.minBlocks));
  outModule.maxBlocks =
      std::max(outModule.minBlocks,
               value.value("max_blocks", outModule.maxBlocks));
  outModule.accentChance =
      std::clamp(value.value("accent_chance", outModule.accentChance),
                 0.0f, 1.0f);
  outModule.requireAir = value.value("require_air", outModule.requireAir);
  return true;
}

json toJson(const BackroomsModule& module) {
  json root = toJson(module.palette);
  root["offset"] = toJson(module.offset);
  root["infinite_y"] = module.infiniteY;
  root["min_y"] = module.minY;
  root["max_y"] = module.maxY;
  root["cell_size"] = toJsonXZ(module.cellSize);
  root["story_height"] = module.storyHeight;
  root["wall_thickness"] = module.wallThickness;
  root["passage_width"] = module.passageWidth;
  root["floor_thickness"] = module.floorThickness;
  root["ceiling_thickness"] = module.ceilingThickness;
  root["passage_chance"] = module.passageChance;
  root["accent_noise_scale"] = module.accentNoiseScale;
  root["accent_threshold"] = module.accentThreshold;
  root["light_chance"] = module.lightChance;
  return root;
}

bool fromJson(const json& value, BackroomsModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "backrooms.settings must be an object.";
    return false;
  }

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "backrooms.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }

  glm::ivec2 cellSize = outModule.cellSize;
  if (!fromJsonXZ(value.value("cell_size", json::object()), cellSize)) {
    outError = "backrooms.settings.cell_size must be an object with x/z.";
    return false;
  }
  outModule.cellSize = clampPositiveIvec2(cellSize, 4);

  outModule.storyHeight =
      std::max(4, value.value("story_height", outModule.storyHeight));
  outModule.wallThickness =
      std::clamp(value.value("wall_thickness", outModule.wallThickness), 1,
                 std::max(1, std::min(outModule.cellSize.x, outModule.cellSize.y) / 2));
  outModule.passageWidth =
      std::clamp(value.value("passage_width", outModule.passageWidth), 1,
                 std::max(1, std::min(outModule.cellSize.x, outModule.cellSize.y) -
                                outModule.wallThickness * 2));
  outModule.floorThickness =
      std::clamp(value.value("floor_thickness", outModule.floorThickness), 1,
                 std::max(1, outModule.storyHeight - 2));
  outModule.ceilingThickness =
      std::clamp(value.value("ceiling_thickness", outModule.ceilingThickness), 1,
                 std::max(1, outModule.storyHeight - outModule.floorThickness - 1));
  outModule.passageChance =
      std::clamp(value.value("passage_chance", outModule.passageChance),
                 0.0f, 1.0f);
  outModule.accentNoiseScale =
      std::max(0.0f,
               value.value("accent_noise_scale", outModule.accentNoiseScale));
  outModule.accentThreshold =
      std::clamp(value.value("accent_threshold", outModule.accentThreshold),
                 -1.0f, 1.0f);
  outModule.lightChance =
      std::clamp(value.value("light_chance", outModule.lightChance), 0.0f, 1.0f);

  return fromJson(value, outModule.palette, outError, "backrooms");
}

json toJson(const MinecraftStyleModule& module) {
  return json{
      {"offset", toJson(module.offset)},
      {"infinite_y", module.infiniteY},
      {"min_y", module.minY},
      {"max_y", module.maxY},
      {"world_height", module.worldHeight},
      {"base_height", module.baseHeight},
      {"height_amplitude", module.heightAmplitude},
      {"detail_amplitude", module.detailAmplitude},
      {"soil_depth", module.soilDepth},
      {"bedrock_thickness", module.bedrockThickness},
      {"terrain_scale", module.terrainScale},
      {"terrain_octaves", module.terrainOctaves},
      {"terrain_persistence", module.terrainPersistence},
      {"detail_scale", module.detailScale},
      {"caves_enabled", module.cavesEnabled},
      {"cave_scale", module.caveScale},
      {"cave_threshold", module.caveThreshold},
      {"cave_warp_scale", module.caveWarpScale},
      {"cave_warp_strength", module.caveWarpStrength},
      {"ore_scale", module.oreScale},
      {"ore_threshold", module.oreThreshold},
      {"surface_block", toJson(module.palette.surfaceRib)},
      {"subsurface_block", toJson(module.palette.surfacePatch)},
      {"stone_block", toJson(module.palette.shell)},
      {"deep_block", toJson(module.palette.core)},
      {"ore_block", toJson(module.palette.accent)},
      {"bedrock_block", toJson(module.palette.recess)},
  };
}

bool fromJson(const json& value, MinecraftStyleModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "minecraft_style.settings must be an object.";
    return false;
  }

  glm::ivec3 offset = outModule.offset;
  if (!fromJson(value.value("offset", json::object()), offset)) {
    outError = "minecraft_style.settings.offset must be an object with x/y/z.";
    return false;
  }
  outModule.offset = offset;

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }
  outModule.worldHeight =
      std::max(16, value.value("world_height", outModule.worldHeight));
  outModule.baseHeight =
      std::clamp(value.value("base_height", outModule.baseHeight), 1,
                 outModule.worldHeight - 2);
  outModule.heightAmplitude =
      std::max(0, value.value("height_amplitude", outModule.heightAmplitude));
  outModule.detailAmplitude =
      std::max(0, value.value("detail_amplitude", outModule.detailAmplitude));
  outModule.soilDepth =
      std::clamp(value.value("soil_depth", outModule.soilDepth), 1,
                 std::max(1, outModule.worldHeight / 2));
  outModule.bedrockThickness =
      std::clamp(value.value("bedrock_thickness", outModule.bedrockThickness), 1,
                 std::max(1, outModule.worldHeight / 4));
  outModule.terrainScale =
      std::max(0.0001f, value.value("terrain_scale", outModule.terrainScale));
  outModule.terrainOctaves =
      std::clamp(value.value("terrain_octaves", outModule.terrainOctaves), 1, 8);
  outModule.terrainPersistence =
      std::clamp(value.value("terrain_persistence", outModule.terrainPersistence),
                 0.05f, 0.99f);
  outModule.detailScale =
      std::max(0.0f, value.value("detail_scale", outModule.detailScale));
  outModule.cavesEnabled =
      value.value("caves_enabled", outModule.cavesEnabled);
  outModule.caveScale =
      std::max(0.0f, value.value("cave_scale", outModule.caveScale));
  outModule.caveThreshold =
      std::clamp(value.value("cave_threshold", outModule.caveThreshold),
                 -1.0f, 1.0f);
  outModule.caveWarpScale =
      std::max(0.0f, value.value("cave_warp_scale", outModule.caveWarpScale));
  outModule.caveWarpStrength =
      std::max(0.0f,
               value.value("cave_warp_strength", outModule.caveWarpStrength));
  outModule.oreScale =
      std::max(0.0f, value.value("ore_scale", outModule.oreScale));
  outModule.oreThreshold =
      std::clamp(value.value("ore_threshold", outModule.oreThreshold),
                 -1.0f, 1.0f);

  if (!fromJson(value.value("surface_block", json()),
                outModule.palette.surfaceRib) ||
      !fromJson(value.value("subsurface_block", json()),
                outModule.palette.surfacePatch) ||
      !fromJson(value.value("stone_block", json()), outModule.palette.shell) ||
      !fromJson(value.value("deep_block", json()), outModule.palette.core) ||
      !fromJson(value.value("ore_block", json()), outModule.palette.accent) ||
      !fromJson(value.value("bedrock_block", json()),
                outModule.palette.recess)) {
    outError = "minecraft_style.settings uses unknown block ids.";
    return false;
  }

  return true;
}

json toJson(const CloudGeneratorModule& module) {
  return json{
      {"cloud_type", cloudTypeId(module.cloudType)},
      {"speed", module.speed},
      {"direction_degrees", module.directionDegrees},
      {"coverage", module.coverage},
      {"opacity", module.opacity},
      {"density_threshold", module.densityThreshold},
      {"cell_size", module.cellSize},
      {"jitter", module.jitter},
      {"layer_height", module.layerHeight},
      {"base_y", module.baseY},
      {"min_y", module.minY},
      {"max_y", module.maxY},
      {"infinite_y", module.infiniteY},
      {"vertical_spacing", module.verticalSpacing},
      {"vertical_jitter", module.verticalJitter},
      {"render_distance", module.renderDistance},
      {"max_mesh_pages_per_frame", module.maxMeshPagesPerFrame},
      {"max_visible_pages", module.maxVisiblePages},
  };
}

bool fromJson(const json& value, CloudGeneratorModule& outModule,
              std::string& outError) {
  if (!value.is_object()) {
    outError = "cloud_generator.settings must be an object.";
    return false;
  }

  CloudType parsedType = outModule.cloudType;
  if (!tryParseCloudType(value.value("cloud_type", std::string("random")),
                         parsedType)) {
    parsedType = CloudType::RANDOM;
  }
  outModule.cloudType = parsedType;

  outModule.speed = std::clamp(value.value("speed", outModule.speed),
                               0.0f, 128.0f);
  outModule.directionDegrees =
      value.value("direction_degrees", outModule.directionDegrees);
  outModule.coverage = std::clamp(value.value("coverage", outModule.coverage),
                                  0.0f, 1.0f);
  outModule.opacity = std::clamp(value.value("opacity", outModule.opacity),
                                 0.05f, 0.85f);
  outModule.densityThreshold =
      std::clamp(value.value("density_threshold", outModule.densityThreshold),
                 0.0f, 1.5f);
  outModule.cellSize =
      std::clamp(value.value("cell_size", outModule.cellSize), 1, 64);
  outModule.jitter =
      std::clamp(value.value("jitter", outModule.jitter), 0, 64);
  outModule.layerHeight =
      std::clamp(value.value("layer_height", outModule.layerHeight), 1, 128);
  outModule.baseY = value.value("base_y", outModule.baseY);
  outModule.minY = value.value("min_y", outModule.minY);
  outModule.maxY = value.value("max_y", outModule.maxY);
  if (outModule.maxY < outModule.minY) {
    std::swap(outModule.minY, outModule.maxY);
  }

  outModule.infiniteY = value.value("infinite_y", outModule.infiniteY);
  outModule.verticalSpacing =
      std::clamp(value.value("vertical_spacing", outModule.verticalSpacing),
                 std::max(outModule.layerHeight + 1, 2), 4096);
  outModule.verticalJitter =
      std::clamp(value.value("vertical_jitter", outModule.verticalJitter),
                 0, outModule.verticalSpacing / 2);
  outModule.renderDistance =
      std::clamp(value.value("render_distance", outModule.renderDistance),
                 1, 16);
  outModule.maxMeshPagesPerFrame =
      std::clamp(value.value("max_mesh_pages_per_frame",
                             outModule.maxMeshPagesPerFrame),
                 1, 64);
  outModule.maxVisiblePages =
      std::clamp(value.value("max_visible_pages", outModule.maxVisiblePages),
                 16, 4096);
  return true;
}

// Funcao: executa 'toJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'json' com o resultado composto por esta chamada.
json toJson(const BiomeModule& module) {
  json root;
  root["format_version"] = module.formatVersion;
  root["id"] = sanitizeModuleId(module.id);
  root["display_name"] = module.displayName;
  root["type"] = moduleTypeId(module.type);
  root["enabled"] = module.enabled;
  root["blend_mode"] = layerBlendModeId(module.blendMode);

  switch (module.type) {
  case ModuleType::PERLIN_TERRAIN:
    root["settings"] = toJson(module.perlinTerrain);
    break;
  case ModuleType::IMPORT_VOX_FILES:
    root["settings"] = toJson(module.importVoxFiles);
    break;
  case ModuleType::GRID_PATTERN:
    root["settings"] = toJson(module.gridPattern);
    break;
  case ModuleType::MENGER_SPONGE:
    root["settings"] = toJson(module.mengerSponge);
    break;
  case ModuleType::CAVE_SYSTEM:
    root["settings"] = toJson(module.caveSystem);
    break;
  case ModuleType::CELLULAR_NOISE:
    root["settings"] = toJson(module.cellularNoise);
    break;
  case ModuleType::FRACTAL_NOISE:
    root["settings"] = toJson(module.fractalNoise);
    break;
  case ModuleType::RIDGED_NOISE:
    root["settings"] = toJson(module.ridgedNoise);
    break;
  case ModuleType::DOMAIN_WARPED_NOISE:
    root["settings"] = toJson(module.domainWarpedNoise);
    break;
  case ModuleType::TREE_GENERATOR:
    root["settings"] = toJson(module.treeGenerator);
    break;
  case ModuleType::FLOATING_ISLANDS:
    root["settings"] = toJson(module.floatingIslands);
    break;
  case ModuleType::ONE_BLOCK:
    root["settings"] = toJson(module.oneBlock);
    break;
  case ModuleType::BACKROOMS:
    root["settings"] = toJson(module.backrooms);
    break;
  case ModuleType::MINECRAFT_STYLE:
    root["settings"] = toJson(module.minecraftStyle);
    break;
  case ModuleType::CLOUD_GENERATOR:
    root["settings"] = toJson(module.cloudGenerator);
    break;
  }

  return root;
}

// Funcao: executa 'fromJson' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'value', 'outModule', 'outError' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool fromJson(const json& value, BiomeModule& outModule, std::string& outError) {
  if (!value.is_object()) {
    outError = "Module entry must be an object.";
    return false;
  }

  BiomeModule module;
  module.formatVersion = value.value("format_version", kBiomeModuleFormatVersion);
  if (module.formatVersion != kBiomeModuleFormatVersion) {
    outError = "Unsupported module format version.";
    return false;
  }

  module.id = sanitizeModuleId(value.value("id", std::string("module")));
  module.displayName = value.value("display_name", std::string("Unnamed Module"));
  module.enabled = value.value("enabled", true);

  ModuleType parsedType = ModuleType::PERLIN_TERRAIN;
  if (!tryParseModuleType(value.value("type", std::string()), parsedType)) {
    outError = "Unsupported module type.";
    return false;
  }
  module.type = parsedType;

  LayerBlendMode blendMode = LayerBlendMode::OVERWRITE_ALL;
  if (!tryParseLayerBlendMode(
          value.value("blend_mode", std::string("overwrite_all")), blendMode)) {
    outError = "Unsupported module blend_mode value.";
    return false;
  }
  module.blendMode = blendMode;

  const json settings = value.value("settings", json::object());
  switch (module.type) {
  case ModuleType::PERLIN_TERRAIN:
    if (!fromJson(settings, module.perlinTerrain, outError)) {
      return false;
    }
    break;
  case ModuleType::IMPORT_VOX_FILES:
    if (!fromJson(settings, module.importVoxFiles, outError)) {
      return false;
    }
    break;
  case ModuleType::GRID_PATTERN:
    if (!fromJson(settings, module.gridPattern, outError)) {
      return false;
    }
    break;
  case ModuleType::MENGER_SPONGE:
    if (!fromJson(settings, module.mengerSponge, outError)) {
      return false;
    }
    break;
  case ModuleType::CAVE_SYSTEM:
    if (!fromJson(settings, module.caveSystem, outError)) {
      return false;
    }
    break;
  case ModuleType::CELLULAR_NOISE:
    if (!fromJson(settings, module.cellularNoise, outError)) {
      return false;
    }
    break;
  case ModuleType::FRACTAL_NOISE:
    if (!fromJson(settings, module.fractalNoise, outError)) {
      return false;
    }
    break;
  case ModuleType::RIDGED_NOISE:
    if (!fromJson(settings, module.ridgedNoise, outError)) {
      return false;
    }
    break;
  case ModuleType::DOMAIN_WARPED_NOISE:
    if (!fromJson(settings, module.domainWarpedNoise, outError)) {
      return false;
    }
    break;
  case ModuleType::TREE_GENERATOR:
    if (!fromJson(settings, module.treeGenerator, outError)) {
      return false;
    }
    break;
  case ModuleType::FLOATING_ISLANDS:
    if (!fromJson(settings, module.floatingIslands, outError)) {
      return false;
    }
    break;
  case ModuleType::ONE_BLOCK:
    if (!fromJson(settings, module.oneBlock, outError)) {
      return false;
    }
    break;
  case ModuleType::BACKROOMS:
    if (!fromJson(settings, module.backrooms, outError)) {
      return false;
    }
    break;
  case ModuleType::MINECRAFT_STYLE:
    if (!fromJson(settings, module.minecraftStyle, outError)) {
      return false;
    }
    break;
  case ModuleType::CLOUD_GENERATOR:
    if (!fromJson(settings, module.cloudGenerator, outError)) {
      return false;
    }
    break;
  }

  outModule = std::move(module);
  return true;
}

// Funcao: carrega 'loadLegacyPresetModules' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'modulesValue', 'preset', 'outError' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool loadLegacyPresetModules(const json& modulesValue, BiomePreset& preset,
                             std::string& outError) {
  if (!modulesValue.is_array() || modulesValue.empty()) {
    outError = "Preset modules array is missing.";
    return false;
  }

  PerlinDensityModule density = legacyDefaultPerlinDensity(preset.preview.depth);
  MaterialPaletteModule palette = legacyDefaultMaterialPalette(preset.preview.depth);
  bool foundDensity = false;
  bool foundPalette = false;

  for (const json& moduleValue : modulesValue) {
    if (!moduleValue.is_object()) {
      outError = "Each legacy module entry must be an object.";
      return false;
    }

    const std::string typeId = moduleValue.value("type", std::string());
    if (typeId == "perlin_density") {
      density.baseScale = moduleValue.value("base_scale", density.baseScale);
      density.densityThreshold =
          moduleValue.value("density_threshold", density.densityThreshold);
      density.densityOctaves =
          std::max(1, moduleValue.value("density_octaves", density.densityOctaves));
      density.densityPersistence =
          moduleValue.value("density_persistence", density.densityPersistence);
      density.warpOctaves =
          std::max(1, moduleValue.value("warp_octaves", density.warpOctaves));
      density.warpStrength =
          moduleValue.value("warp_strength", density.warpStrength);
      foundDensity = true;
      continue;
    }

    if (typeId == "material_palette") {
      if (!fromJson(moduleValue.value("surface_rib", json()), palette.surfaceRib) ||
          !fromJson(moduleValue.value("surface_patch", json()), palette.surfacePatch) ||
          !fromJson(moduleValue.value("shell", json()), palette.shell) ||
          !fromJson(moduleValue.value("core", json()), palette.core) ||
          !fromJson(moduleValue.value("accent", json()), palette.accent) ||
          !fromJson(moduleValue.value("recess", json()), palette.recess)) {
        outError = "Legacy material_palette uses unknown block ids.";
        return false;
      }
      foundPalette = true;
      continue;
    }
  }

  if (!foundDensity || !foundPalette) {
    outError =
        "Legacy preset must contain perlin_density and material_palette modules.";
    return false;
  }

  BiomeModule module =
      BiomeModule::makePerlinTerrain("perlin", preset.preview.depth);
  module.perlinTerrain.density = density;
  module.perlinTerrain.palette = palette;
  preset.modules = {module};
  return true;
}

// Funcao: resolve 'resolveModuleReference' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'presetPath', 'moduleRef' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
// Retorno: devolve 'std::filesystem::path' com o resultado composto por esta chamada.
std::filesystem::path resolveModuleReference(const std::filesystem::path& presetPath,
                                             const std::filesystem::path& moduleRef) {
  if (moduleRef.is_absolute()) {
    return moduleRef;
  }
  return presetPath.parent_path() / moduleRef;
}

// Funcao: carrega 'loadBiomeModuleDocument' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'path', 'outModule', 'outError' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool loadBiomeModuleDocument(const std::filesystem::path& path, BiomeModule& outModule,
                             std::string& outError) {
  outError.clear();

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    outError = "Failed to open module file.";
    return false;
  }

  json root = json::parse(file, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    outError = "Module file is not valid JSON.";
    return false;
  }

  return fromJson(root, outModule, outError);
}

// Funcao: carrega 'loadPresetModules' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'presetPath', 'modulesValue', 'preset', 'outError' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool loadPresetModules(const std::filesystem::path& presetPath, const json& modulesValue,
                       BiomePreset& preset, std::string& outError) {
  if (!modulesValue.is_array() || modulesValue.empty()) {
    outError = "Preset modules array is missing.";
    return false;
  }

  preset.modules.clear();
  preset.modules.reserve(modulesValue.size());
  for (const json& moduleEntry : modulesValue) {
    BiomeModule module;
    if (moduleEntry.is_string()) {
      const std::filesystem::path moduleRef = moduleEntry.get<std::string>();
      const std::filesystem::path modulePath =
          resolveModuleReference(presetPath, moduleRef);
      if (!loadBiomeModuleDocument(modulePath, module, outError)) {
        outError = "Failed to load module '" + moduleRef.generic_string() +
                   "': " + outError;
        return false;
      }
      module.filePath = moduleRef;
      preset.modules.push_back(std::move(module));
      continue;
    }

    if (!moduleEntry.is_object()) {
      outError = "Preset module entries must be objects or file paths.";
      return false;
    }

    if (moduleEntry.contains("file")) {
      const std::filesystem::path moduleRef = moduleEntry.value("file", std::string());
      if (moduleRef.empty()) {
        outError = "Preset module entry is missing a file path.";
        return false;
      }

      const std::filesystem::path modulePath =
          resolveModuleReference(presetPath, moduleRef);
      if (!loadBiomeModuleDocument(modulePath, module, outError)) {
        outError = "Failed to load module '" + moduleRef.generic_string() +
                   "': " + outError;
        return false;
      }
      module.filePath = moduleRef;
      preset.modules.push_back(std::move(module));
      continue;
    }

    if (!fromJson(moduleEntry, module, outError)) {
      return false;
    }
    preset.modules.push_back(std::move(module));
  }

  return true;
}

} // namespace

// Funcao: executa 'suggestModulePath' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'preset', 'module' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'std::filesystem::path' com o resultado composto por esta chamada.
std::filesystem::path suggestModulePath(const BiomePreset& preset,
                                        const BiomeModule& module) {
  (void)preset;
  return (std::filesystem::path("..") / "Modules") /
         (sanitizeModuleId(module.id) + ".fvmodule.json");
}

// Funcao: carrega 'loadBiomeModuleFromFile' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'path', 'outModule', 'outError' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool loadBiomeModuleFromFile(const std::filesystem::path& path, BiomeModule& outModule,
                             std::string& outError) {
  return loadBiomeModuleDocument(path, outModule, outError);
}

// Funcao: salva 'saveBiomeModuleToFile' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'path', 'module', 'outError' para persistir os dados recebidos no formato esperado pelo projeto.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool saveBiomeModuleToFile(const std::filesystem::path& path,
                           const BiomeModule& module, std::string& outError) {
  outError.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    outError = "Failed to create module directory.";
    return false;
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    outError = "Failed to open module file for writing.";
    return false;
  }

  file << toJson(module).dump(2);
  return file.good();
}

// Funcao: carrega 'loadBiomePresetFromFile' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'path', 'outPreset', 'outError' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool loadBiomePresetFromFile(const std::filesystem::path& path, BiomePreset& outPreset,
                             std::string& outError) {
  outError.clear();
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    outError = "Failed to open preset file.";
    return false;
  }

  json root = json::parse(file, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    outError = "Preset file is not valid JSON.";
    return false;
  }

  BiomePreset preset;
  preset.formatVersion = root.value("format_version", 1);
  preset.id = sanitizePresetId(root.value("id", std::string("biome_preset")));
  preset.displayName = root.value("display_name", std::string("Unnamed Preset"));

  if (!fromJson(root.value("preview", json::object()), preset.preview, outError)) {
    return false;
  }

  const json modulesValue = root.value("modules", json::array());
  if (preset.formatVersion == 1) {
    if (!loadLegacyPresetModules(modulesValue, preset, outError)) {
      return false;
    }
    outPreset = preset;
    return true;
  }

  if (preset.formatVersion < 1 || preset.formatVersion > kBiomePresetFormatVersion) {
    outError = "Unsupported preset format version.";
    return false;
  }

  if (!loadPresetModules(path, modulesValue, preset, outError)) {
    return false;
  }

  outPreset = std::move(preset);
  return true;
}

// Funcao: salva 'saveBiomePresetToFile' na serializacao e utilitarios de preset de biome.
// Detalhe: usa 'path', 'preset', 'outError' para persistir os dados recebidos no formato esperado pelo projeto.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool saveBiomePresetToFile(const std::filesystem::path& path, const BiomePreset& preset,
                           std::string& outError) {
  outError.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    outError = "Failed to create preset directory.";
    return false;
  }

  if (preset.modules.empty()) {
    outError = "Preset must contain at least one module.";
    return false;
  }

  json root;
  root["format_version"] = kBiomePresetFormatVersion;
  root["id"] = sanitizePresetId(preset.id);
  root["display_name"] = preset.displayName;
  root["preview"] = toJson(preset.preview);
  root["modules"] = json::array();

  for (const BiomeModule& module : preset.modules) {
    root["modules"].push_back(toJson(module));
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    outError = "Failed to open preset file for writing.";
    return false;
  }

  file << root.dump(2);
  return file.good();
}

} // namespace VoxelGame
#pragma endregion
