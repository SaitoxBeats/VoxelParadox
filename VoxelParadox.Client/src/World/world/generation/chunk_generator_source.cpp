// Arquivo: VoxelParadox.Client/src/World/world/chunk_generator_source.cpp
// Papel: implementa "chunk generator source" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "world/generation/chunk_generator_source.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <unordered_map>

#include "path/app_paths.hpp"
#include "world/generation/noise.hpp"
#pragma endregion

#pragma region ChunkGeneratorLocalHelpers
namespace VoxelGame {
namespace {

// Funcao: executa 'saturate' na geracao procedural baseada em presets.
// Detalhe: usa 'value' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

// Funcao: executa 'positiveMod' na geracao procedural baseada em presets.
// Detalhe: usa 'value', 'modulus' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
float positiveMod(float value, float modulus) {
  if (modulus <= 0.0f) {
    return 0.0f;
  }
  float wrapped = std::fmod(value, modulus);
  if (wrapped < 0.0f) {
    wrapped += modulus;
  }
  return wrapped;
}

// Funcao: executa 'positiveModInt' na geracao procedural baseada em presets.
// Detalhe: usa 'value', 'modulus' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
int positiveModInt(int value, int modulus) {
  if (modulus <= 0) {
    return 0;
  }
  int wrapped = value % modulus;
  if (wrapped < 0) {
    wrapped += modulus;
  }
  return wrapped;
}

// Funcao: executa 'chunkIntersectsVerticalRange' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'infiniteY', 'minY', 'maxY' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool chunkIntersectsVerticalRange(const Chunk& chunk, bool infiniteY, int minY,
                                  int maxY) {
  if (infiniteY) {
    return true;
  }

  const int chunkMinY = chunk.chunkPos.y * Chunk::SIZE;
  const int chunkMaxY = chunkMinY + Chunk::SIZE - 1;
  return !(chunkMaxY < minY || chunkMinY > maxY);
}

float thresholdMargin(float signal, float threshold, bool invert) {
  return invert ? (threshold - signal) : (signal - threshold);
}

bool containsBlockType(const std::vector<BlockId>& blockTypes,
                       BlockId candidate) {
  return std::find(blockTypes.begin(), blockTypes.end(), candidate) !=
         blockTypes.end();
}

struct ChunkCacheKey {
  int x = 0;
  int y = 0;
  int z = 0;

  bool operator==(const ChunkCacheKey& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct ChunkCacheKeyHasher {
  std::size_t operator()(const ChunkCacheKey& key) const {
    std::size_t hash = static_cast<std::size_t>(static_cast<std::uint32_t>(key.x));
    hash = (hash * 16777619ull) ^
           static_cast<std::size_t>(static_cast<std::uint32_t>(key.y));
    hash = (hash * 16777619ull) ^
           static_cast<std::size_t>(static_cast<std::uint32_t>(key.z));
    return hash;
  }
};

int treeCellSize(float density, VoxPlacementPattern pattern) {
  const float clampedDensity = std::clamp(density, 0.0f, 1.0f);
  const float minCell = pattern == VoxPlacementPattern::GRID ? 6.0f : 7.0f;
  const float maxCell = pattern == VoxPlacementPattern::GRID ? 20.0f : 18.0f;
  return std::max(4, static_cast<int>(
                         std::round(glm::mix(maxCell, minCell, clampedDensity))));
}

int treeJitter(int cellSize) { return std::max(0, (cellSize / 2) - 1); }

int treeHorizontalReach(TreeGeneratorType type) {
  switch (type) {
  case TreeGeneratorType::NORMAL:
    return 3;
  case TreeGeneratorType::STRANGE:
    return 2;
  case TreeGeneratorType::TRUNK_ONLY:
    return 0;
  default:
    return 3;
  }
}

int treeVerticalReach(TreeGeneratorType type) {
  switch (type) {
  case TreeGeneratorType::NORMAL:
    return 10;
  case TreeGeneratorType::STRANGE:
    return 12;
  case TreeGeneratorType::TRUNK_ONLY:
    return 8;
  default:
    return 10;
  }
}

} // namespace

// Funcao: executa 'PresetModuleGeneratorSource' na geracao procedural baseada em presets.
// Detalhe: usa 'preset' para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region GeneratorConstruction
PresetModuleGeneratorSource::PresetModuleGeneratorSource(const BiomePreset& preset)
    : PresetModuleGeneratorSource(
          std::shared_ptr<const BiomePreset>(&preset, [](const BiomePreset*) {}),
          preset.preview.depth, preset.preview.seed) {}

// Funcao: executa 'PresetModuleGeneratorSource' na geracao procedural baseada em presets.
// Detalhe: usa 'preset', 'runtimeDepth', 'runtimeSeed' para encapsular esta etapa especifica do subsistema.
PresetModuleGeneratorSource::PresetModuleGeneratorSource(
    std::shared_ptr<const BiomePreset> preset, int runtimeDepth,
    std::uint32_t runtimeSeed)
    : preset_(std::move(preset)), runtimeDepth_(runtimeDepth),
      runtimeSeed_(runtimeSeed) {
  if (!preset_) {
    return;
  }

  importVoxRuntimes_.reserve(preset_->modules.size());
  for (std::size_t index = 0; index < preset_->modules.size(); index++) {
    const BiomeModule& module = preset_->modules[index];
    if (module.type != ModuleType::IMPORT_VOX_FILES) {
      continue;
    }

    ImportVoxRuntime runtime;
    runtime.moduleIndex = index;
    runtime.module = module;
    runtime.files = collectVoxFiles(module.importVoxFiles.sourceDirectory,
                                    module.importVoxFiles.includeSubdirectories);
    runtime.moduleHash =
        hashString(module.id + "|" + module.importVoxFiles.sourceDirectory) ^
        module.importVoxFiles.seedOffset;

    for (const std::string& file : runtime.files) {
      const VoxStructureData* structure =
          loadVoxStructure(
              file, module.importVoxFiles.colorMapping,
              module.importVoxFiles.colorMapping == VoxColorMapping::DEFAULT
                  ? std::optional<BlockId>(module.importVoxFiles.defaultVoxel)
                  : std::nullopt);
      if (!structure) {
        continue;
      }
      runtime.maxStructureSize =
          glm::max(runtime.maxStructureSize, structure->size);
    }

    importVoxRuntimes_.push_back(std::move(runtime));
  }
}
#pragma endregion

#pragma region GeneratorMathAndHashing
PresetModuleGeneratorSource::SeedOffset
// Funcao: monta 'makeSeedOffset' na geracao procedural baseada em presets.
// Detalhe: usa 'seed' para derivar e compor um valor pronto para a proxima etapa do pipeline.
PresetModuleGeneratorSource::makeSeedOffset(std::uint32_t seed) {
  SeedOffset offset;
  offset.x = (seed & 0xFFu) * 0.3713f;
  offset.y = ((seed >> 8) & 0xFFu) * 0.2918f;
  offset.z = ((seed >> 16) & 0xFFu) * 0.4517f;
  return offset;
}

// Funcao: avalia 'seededFbm' na geracao procedural baseada em presets.
// Detalhe: usa 'x', 'y', 'z', 'offset', 'octaves', 'persistence' para produzir o valor de ruido usado pelas camadas procedurais do jogo.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
float PresetModuleGeneratorSource::seededFbm(float x, float y, float z,
                                             const SeedOffset& offset, int octaves,
                                             float persistence,
                                             float lacunarity) {
  return noise::fbm(x + offset.x, y + offset.y, z + offset.z, octaves,
                    persistence, lacunarity);
}

float PresetModuleGeneratorSource::seededRidgedFbm(
    float x, float y, float z, const SeedOffset& offset, int octaves,
    float persistence, float lacunarity, float ridgeSharpness) {
  return noise::ridgedFbm(x + offset.x, y + offset.y, z + offset.z, octaves,
                          persistence, lacunarity, ridgeSharpness);
}

// Funcao: executa 'floorDiv' na geracao procedural baseada em presets.
// Detalhe: usa 'value', 'divisor' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
int PresetModuleGeneratorSource::floorDiv(int value, int divisor) {
  return value >= 0 ? (value / divisor) : ((value - (divisor - 1)) / divisor);
}

// Funcao: calcula 'hash3i' na geracao procedural baseada em presets.
// Detalhe: usa 'x', 'y', 'z', 'seed' para produzir um identificador deterministico usado em cache, lookup ou seed.
// Retorno: devolve 'std::uint32_t' com o valor numerico calculado para a proxima decisao do pipeline.
std::uint32_t PresetModuleGeneratorSource::hash3i(int x, int y, int z,
                                                  std::uint32_t seed) {
  std::uint32_t hash = seed;
  hash ^= static_cast<std::uint32_t>(x) * 0x9e3779b1u;
  hash ^= static_cast<std::uint32_t>(y) * 0x85ebca77u;
  hash ^= static_cast<std::uint32_t>(z) * 0xc2b2ae3du;
  hash ^= hash >> 16;
  hash *= 0x7feb352du;
  hash ^= hash >> 15;
  hash *= 0x846ca68bu;
  hash ^= hash >> 16;
  return hash;
}

// Funcao: calcula 'hash01' na geracao procedural baseada em presets.
// Detalhe: usa 'value' para produzir um identificador deterministico usado em cache, lookup ou seed.
// Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
float PresetModuleGeneratorSource::hash01(std::uint32_t value) {
  return static_cast<float>(value & 0x00FFFFFFu) / 16777216.0f;
}

// Funcao: executa 'randRange' na geracao procedural baseada em presets.
// Detalhe: usa 'state', 'minInclusive', 'maxInclusive' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
int PresetModuleGeneratorSource::randRange(std::uint32_t& state, int minInclusive,
                                           int maxInclusive) {
  state = state * 1664525u + 1013904223u;
  if (maxInclusive <= minInclusive) {
    return minInclusive;
  }
  const std::uint32_t span = static_cast<std::uint32_t>(maxInclusive - minInclusive + 1);
  return minInclusive + static_cast<int>(state % span);
}

// Funcao: calcula 'hashString' na geracao procedural baseada em presets.
// Detalhe: usa 'value' para produzir um identificador deterministico usado em cache, lookup ou seed.
// Retorno: devolve 'std::uint32_t' com o valor numerico calculado para a proxima decisao do pipeline.
std::uint32_t PresetModuleGeneratorSource::hashString(const std::string& value) {
  std::uint32_t hash = 2166136261u;
  for (unsigned char ch : value) {
    hash ^= static_cast<std::uint32_t>(ch);
    hash *= 16777619u;
  }
  return hash;
}

// Funcao: executa 'rotateVoxPositionY' na geracao procedural baseada em presets.
// Detalhe: usa 'position', 'size', 'rotation' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
glm::ivec3 PresetModuleGeneratorSource::rotateVoxPositionY(
    const glm::ivec3& position, const glm::ivec3& size, int rotation) {
  switch (rotation & 3) {
  case 1:
    return glm::ivec3(position.z, position.y, size.x - 1 - position.x);
  case 2:
    return glm::ivec3(size.x - 1 - position.x, position.y,
                      size.z - 1 - position.z);
  case 3:
    return glm::ivec3(size.z - 1 - position.z, position.y, position.x);
  default:
    return position;
  }
}

// Funcao: executa 'rotatedVoxSizeY' na geracao procedural baseada em presets.
// Detalhe: usa 'size', 'rotation' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
glm::ivec3 PresetModuleGeneratorSource::rotatedVoxSizeY(const glm::ivec3& size,
                                                        int rotation) {
  return ((rotation & 1) == 0) ? size : glm::ivec3(size.z, size.y, size.x);
}

// Funcao: executa 'writeLayerBlock' na geracao procedural baseada em presets.
// Detalhe: usa 'target', 'value', 'blendMode' para encapsular esta etapa especifica do subsistema.
void PresetModuleGeneratorSource::writeLayerBlock(BlockId& target, BlockId value,
                                                  LayerBlendMode blendMode) {
  switch (blendMode) {
  case LayerBlendMode::OVERWRITE_ALL:
    target = value;
    break;
  case LayerBlendMode::PLACE_SOLIDS:
    if (value != BlockIds::AIR) {
      target = value;
    }
    break;
  case LayerBlendMode::PLACE_ON_AIR:
    if (value != BlockIds::AIR && target == BlockIds::AIR) {
      target = value;
    }
    break;
  }
}

// Funcao: executa 'collectVoxFiles' na geracao procedural baseada em presets.
// Detalhe: usa 'root', 'recursive' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'std::vector<std::string>' com o texto pronto para exibicao, lookup ou serializacao.
std::vector<std::string> PresetModuleGeneratorSource::collectVoxFiles(
    const std::string& root, bool recursive) {
  if (recursive) {
    return getVoxFilesRecursive(root);
  }

  std::vector<std::string> files;
  const std::filesystem::path rootPath = AppPaths::resolve(root);
  std::error_code ec;
  if (!std::filesystem::exists(rootPath, ec)) {
    return files;
  }

  for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() == ".vox") {
      files.push_back(entry.path().generic_string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

const PresetModuleGeneratorSource::ImportVoxRuntime*
// Funcao: procura 'findImportRuntime' na geracao procedural baseada em presets.
// Detalhe: usa 'moduleIndex' para localizar o primeiro elemento que atende ao criterio esperado.
PresetModuleGeneratorSource::findImportRuntime(std::size_t moduleIndex) const {
  for (const ImportVoxRuntime& runtime : importVoxRuntimes_) {
    if (runtime.moduleIndex == moduleIndex) {
      return &runtime;
    }
  }
  return nullptr;
}

// Funcao: executa 'fillAir' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk' para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region GeneratorChunkPipeline
void PresetModuleGeneratorSource::fillAir(Chunk& chunk) const {
  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      for (int z = 0; z < Chunk::SIZE; z++) {
        chunk.blocks[x][y][z] = BlockIds::AIR;
      }
    }
  }
}

void PresetModuleGeneratorSource::generateBaseChunk(Chunk& chunk) const {
  fillAir(chunk);

  for (std::size_t reverseIndex = preset_->modules.size(); reverseIndex > 0;
       reverseIndex--) {
    const std::size_t index = reverseIndex - 1;
    const BiomeModule& module = preset_->modules[index];
    if (!module.enabled || module.type == ModuleType::TREE_GENERATOR ||
        module.type == ModuleType::ONE_BLOCK ||
        module.type == ModuleType::CLOUD_GENERATOR) {
      continue;
    }

    applyModuleLayer(chunk, module, index);
  }

  carveSpawnBubble(chunk);
  chunk.generated = true;
}

void PresetModuleGeneratorSource::applyModuleLayer(
    Chunk& chunk, const BiomeModule& module, std::size_t moduleIndex) const {
  switch (module.type) {
  case ModuleType::PERLIN_TERRAIN:
    applyPerlinTerrainLayer(chunk, module);
    break;
  case ModuleType::IMPORT_VOX_FILES:
    if (const ImportVoxRuntime* runtime = findImportRuntime(moduleIndex)) {
      applyImportVoxLayer(chunk, *runtime);
    }
    break;
  case ModuleType::GRID_PATTERN:
    applyGridPatternLayer(chunk, module);
    break;
  case ModuleType::MENGER_SPONGE:
    applyMengerSpongeLayer(chunk, module);
    break;
  case ModuleType::CAVE_SYSTEM:
    applyCaveSystemLayer(chunk, module);
    break;
  case ModuleType::CELLULAR_NOISE:
    applyCellularNoiseLayer(chunk, module);
    break;
  case ModuleType::FRACTAL_NOISE:
    applyFractalNoiseLayer(chunk, module);
    break;
  case ModuleType::RIDGED_NOISE:
    applyRidgedNoiseLayer(chunk, module);
    break;
  case ModuleType::DOMAIN_WARPED_NOISE:
    applyDomainWarpedNoiseLayer(chunk, module);
    break;
  case ModuleType::TREE_GENERATOR:
    applyTreeGeneratorLayer(chunk, module);
    break;
  case ModuleType::FLOATING_ISLANDS:
    applyFloatingIslandsLayer(chunk, module);
    break;
  case ModuleType::ONE_BLOCK:
    applyOneBlockLayer(chunk, module);
    break;
  case ModuleType::BACKROOMS:
    applyBackroomsLayer(chunk, module);
    break;
  case ModuleType::MINECRAFT_STYLE:
    applyMinecraftStyleLayer(chunk, module);
    break;
  case ModuleType::CLOUD_GENERATOR:
    break;
  }
}

// Funcao: executa 'generateChunk' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk' para encapsular esta etapa especifica do subsistema.
void PresetModuleGeneratorSource::generateChunk(Chunk& chunk) const {
  // A ordem reversa faz o primeiro modulo da lista do preset agir como camada de maior prioridade.
  fillAir(chunk);

  for (std::size_t reverseIndex = preset_->modules.size(); reverseIndex > 0;
       reverseIndex--) {
    const std::size_t index = reverseIndex - 1;
    const BiomeModule& module = preset_->modules[index];
    if (!module.enabled) {
      continue;
    }
    if (module.type == ModuleType::CLOUD_GENERATOR) {
      continue;
    }

    applyModuleLayer(chunk, module, index);
  }

  carveSpawnBubble(chunk);
  applyTopDecorationBlocks(chunk);
  chunk.generated = true;
}

// Funcao: aplica 'applyPerlinTerrainLayer' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'module' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
#pragma endregion

#pragma region PerlinTerrainModule
void PresetModuleGeneratorSource::applyPerlinTerrainLayer(
    Chunk& chunk, const BiomeModule& module) const {
  // O modulo perlin gera a massa principal e escolhe o material final com base em profundidade e campos auxiliares.
  const PerlinTerrainModule& perlin = module.perlinTerrain;
  const PerlinDensityModule& density = perlin.density;
  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool skipOccupiedAirTargets =
      module.blendMode == LayerBlendMode::PLACE_ON_AIR;

  float xs[Chunk::SIZE];
  float ys[Chunk::SIZE];
  float zs[Chunk::SIZE];
  for (int x = 0; x < Chunk::SIZE; x++) {
    xs[x] = (wx0 + x) * density.baseScale;
  }
  for (int y = 0; y < Chunk::SIZE; y++) {
    ys[y] = (wy0 + y) * density.baseScale;
  }
  for (int z = 0; z < Chunk::SIZE; z++) {
    zs[z] = (wz0 + z) * density.baseScale;
  }

  const SeedOffset warpOffsetX = makeSeedOffset(seed());
  const SeedOffset warpOffsetY = makeSeedOffset(seed() + 111u);
  const SeedOffset warpOffsetZ = makeSeedOffset(seed() + 222u);
  const SeedOffset densityOffset = makeSeedOffset(seed());
  const SeedOffset regionOffset = makeSeedOffset(seed() ^ 0xA5317A4Du);
  const SeedOffset patchOffset = makeSeedOffset(seed() ^ 0x6C8E9CF5u);
  const SeedOffset accentOffset = makeSeedOffset(seed() ^ 0x9E3779B9u);

  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      for (int z = 0; z < Chunk::SIZE; z++) {
        if (skipOccupiedAirTargets && chunk.blocks[x][y][z] != BlockIds::AIR) {
          continue;
        }

        const float wx = xs[x];
        const float wy = ys[y];
        const float wz = zs[z];

        const float warpX =
            seededFbm(wx + 50.0f, wy, wz, warpOffsetX,
                      density.warpOctaves, 0.5f) *
            density.warpStrength;
        const float warpY =
            seededFbm(wx, wy + 50.0f, wz, warpOffsetY,
                      density.warpOctaves, 0.5f) *
            density.warpStrength;
        const float warpZ =
            seededFbm(wx, wy, wz + 50.0f, warpOffsetZ,
                      density.warpOctaves, 0.5f) *
            density.warpStrength;

        const float densityValue =
            seededFbm(wx + warpX, wy + warpY, wz + warpZ, densityOffset,
                      density.densityOctaves, density.densityPersistence);

        const BlockId value =
            densityValue > density.densityThreshold
                ? pickPerlinBlockType(perlin, wx0 + x, wy0 + y, wz0 + z,
                                      densityValue, regionOffset, patchOffset,
                                      accentOffset)
                : BlockIds::AIR;
        if (value == BlockIds::AIR) {
          continue;
        }
        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

// Funcao: aplica 'applyGridPatternLayer' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'module' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
#pragma endregion

#pragma region GridPatternModule
void PresetModuleGeneratorSource::applyGridPatternLayer(
    Chunk& chunk, const BiomeModule& module) const {
  // A versao sem warp tenta pre-computar o maximo possivel por eixo para reduzir custo por voxel.
  const GridPatternModule& grid = module.gridPattern;
  if (!chunkIntersectsVerticalRange(chunk, grid.infiniteY, grid.minY, grid.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useWarp = grid.warpStrength > 0.0f && grid.warpScale > 0.0f;
  const bool useAccentNoise = grid.accentNoiseScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset warpOffsetX = makeSeedOffset(moduleSeed ^ 0x11f123b5u);
  const SeedOffset warpOffsetY = makeSeedOffset(moduleSeed ^ 0x4a39b70du);
  const SeedOffset warpOffsetZ = makeSeedOffset(moduleSeed ^ 0x9e3779b9u);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0xc2b2ae35u);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; x++) {
    baseX[x] = static_cast<float>(wx0 + x - grid.offset.x);
  }
  for (int y = 0; y < Chunk::SIZE; y++) {
    baseY[y] = static_cast<float>(wy0 + y - grid.offset.y);
  }
  for (int z = 0; z < Chunk::SIZE; z++) {
    baseZ[z] = static_cast<float>(wz0 + z - grid.offset.z);
  }

  if (!useWarp) {
    std::array<std::uint8_t, Chunk::SIZE> nearX{};
    std::array<std::uint8_t, Chunk::SIZE> nearY{};
    std::array<std::uint8_t, Chunk::SIZE> nearZ{};

    for (int x = 0; x < Chunk::SIZE; x++) {
      const int localX = positiveModInt(wx0 + x - grid.offset.x, grid.cellSize.x);
      nearX[x] = static_cast<std::uint8_t>(
          localX < grid.lineWidth.x || localX >= grid.cellSize.x - grid.lineWidth.x);
    }
    for (int y = 0; y < Chunk::SIZE; y++) {
      const int localY = positiveModInt(wy0 + y - grid.offset.y, grid.cellSize.y);
      nearY[y] = static_cast<std::uint8_t>(
          localY < grid.lineWidth.y || localY >= grid.cellSize.y - grid.lineWidth.y);
    }
    for (int z = 0; z < Chunk::SIZE; z++) {
      const int localZ = positiveModInt(wz0 + z - grid.offset.z, grid.cellSize.z);
      nearZ[z] = static_cast<std::uint8_t>(
          localZ < grid.lineWidth.z || localZ >= grid.cellSize.z - grid.lineWidth.z);
    }

    for (int x = 0; x < Chunk::SIZE; x++) {
      for (int y = 0; y < Chunk::SIZE; y++) {
        const int worldY = wy0 + y;
        if (!grid.infiniteY && (worldY < grid.minY || worldY > grid.maxY)) {
          continue;
        }

        for (int z = 0; z < Chunk::SIZE; z++) {
          const int axisMatches = static_cast<int>(nearX[x]) +
                                  static_cast<int>(nearY[y]) +
                                  static_cast<int>(nearZ[z]);
          if (axisMatches < grid.minAxisMatches ||
              axisMatches > grid.maxAxisMatches) {
            continue;
          }

          BlockId value = grid.palette.primary;
          if (axisMatches >= 3) {
            value = grid.palette.accent;
          } else if (axisMatches == 2) {
            value = grid.palette.secondary;
          }

          if (useAccentNoise) {
            const float accentNoise =
                seededFbm(baseX[x] * grid.accentNoiseScale,
                          baseY[y] * grid.accentNoiseScale,
                          baseZ[z] * grid.accentNoiseScale, accentOffset, 2, 0.5f);
            if (accentNoise > grid.accentThreshold) {
              value = grid.palette.accent;
            }
          }

          writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
        }
      }
    }
    return;
  }

  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      const int worldY = wy0 + y;
      if (!grid.infiniteY && (worldY < grid.minY || worldY > grid.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; z++) {
        glm::vec3 point(baseX[x], baseY[y], baseZ[z]);
        const float sx = point.x * grid.warpScale;
        const float sy = point.y * grid.warpScale;
        const float sz = point.z * grid.warpScale;
        point.x += seededFbm(sx + 31.0f, sy, sz, warpOffsetX, 2, 0.5f) *
                   grid.warpStrength;
        point.y += seededFbm(sx, sy + 31.0f, sz, warpOffsetY, 2, 0.5f) *
                   grid.warpStrength;
        point.z += seededFbm(sx, sy, sz + 31.0f, warpOffsetZ, 2, 0.5f) *
                   grid.warpStrength;

        const float localX = positiveMod(point.x, static_cast<float>(grid.cellSize.x));
        const float localY = positiveMod(point.y, static_cast<float>(grid.cellSize.y));
        const float localZ = positiveMod(point.z, static_cast<float>(grid.cellSize.z));

        const bool nearX =
            localX < grid.lineWidth.x ||
            localX >= static_cast<float>(grid.cellSize.x - grid.lineWidth.x);
        const bool nearY =
            localY < grid.lineWidth.y ||
            localY >= static_cast<float>(grid.cellSize.y - grid.lineWidth.y);
        const bool nearZ =
            localZ < grid.lineWidth.z ||
            localZ >= static_cast<float>(grid.cellSize.z - grid.lineWidth.z);

        const int axisMatches = static_cast<int>(nearX) + static_cast<int>(nearY) +
                                static_cast<int>(nearZ);
        if (axisMatches < grid.minAxisMatches ||
            axisMatches > grid.maxAxisMatches) {
          continue;
        }

        BlockId value = grid.palette.primary;
        if (axisMatches >= 3) {
          value = grid.palette.accent;
        } else if (axisMatches == 2) {
          value = grid.palette.secondary;
        }

        if (useAccentNoise) {
          const float accentNoise =
              seededFbm(point.x * grid.accentNoiseScale,
                        point.y * grid.accentNoiseScale,
                        point.z * grid.accentNoiseScale, accentOffset, 2, 0.5f);
          if (accentNoise > grid.accentThreshold) {
            value = grid.palette.accent;
          }
        }

        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

// Funcao: aplica 'applyMengerSpongeLayer' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'module' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
#pragma endregion

#pragma region MengerSpongeModule
void PresetModuleGeneratorSource::applyMengerSpongeLayer(
    Chunk& chunk, const BiomeModule& module) const {
  // A logica combina uma regra discreta de carve com ruido secundario para evitar um resultado visual muito uniforme.
  const MengerSpongeModule& sponge = module.mengerSponge;
  if (!chunkIntersectsVerticalRange(chunk, sponge.infiniteY, sponge.minY, sponge.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useWarp = sponge.warpStrength > 0.0f && sponge.warpScale > 0.0f;
  const bool useAccentNoise = sponge.accentNoiseScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset warpOffsetX = makeSeedOffset(moduleSeed ^ 0x243f6a88u);
  const SeedOffset warpOffsetY = makeSeedOffset(moduleSeed ^ 0x85a308d3u);
  const SeedOffset warpOffsetZ = makeSeedOffset(moduleSeed ^ 0x13198a2eu);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0xa4093822u);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0x299f31d0u);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; x++) {
    baseX[x] = static_cast<float>(wx0 + x - sponge.offset.x);
  }
  for (int y = 0; y < Chunk::SIZE; y++) {
    baseY[y] = static_cast<float>(wy0 + y - sponge.offset.y);
  }
  for (int z = 0; z < Chunk::SIZE; z++) {
    baseZ[z] = static_cast<float>(wz0 + z - sponge.offset.z);
  }

  if (!useWarp) {
    std::array<float, Chunk::SIZE> localX{};
    std::array<float, Chunk::SIZE> localY{};
    std::array<float, Chunk::SIZE> localZ{};
    const float invCellX = 1.0f / static_cast<float>(sponge.cellSize.x);
    const float invCellY = 1.0f / static_cast<float>(sponge.cellSize.y);
    const float invCellZ = 1.0f / static_cast<float>(sponge.cellSize.z);

    for (int x = 0; x < Chunk::SIZE; x++) {
      localX[x] = static_cast<float>(
                      positiveModInt(wx0 + x - sponge.offset.x, sponge.cellSize.x)) *
                  invCellX;
    }
    for (int y = 0; y < Chunk::SIZE; y++) {
      localY[y] = static_cast<float>(
                      positiveModInt(wy0 + y - sponge.offset.y, sponge.cellSize.y)) *
                  invCellY;
    }
    for (int z = 0; z < Chunk::SIZE; z++) {
      localZ[z] = static_cast<float>(
                      positiveModInt(wz0 + z - sponge.offset.z, sponge.cellSize.z)) *
                  invCellZ;
    }

    for (int x = 0; x < Chunk::SIZE; x++) {
      for (int y = 0; y < Chunk::SIZE; y++) {
        const int worldY = wy0 + y;
        if (!sponge.infiniteY && (worldY < sponge.minY || worldY > sponge.maxY)) {
          continue;
        }

        for (int z = 0; z < Chunk::SIZE; z++) {
          glm::vec3 local(localX[x], localY[y], localZ[z]);
          bool carved = false;
          float secondarySignal = 0.0f;
          for (int level = 0; level < sponge.iterations; level++) {
            local *= 3.0f;
            glm::ivec3 cell(static_cast<int>(std::floor(local.x)),
                            static_cast<int>(std::floor(local.y)),
                            static_cast<int>(std::floor(local.z)));
            cell = glm::clamp(cell, glm::ivec3(0), glm::ivec3(2));

            const int middleAxes = static_cast<int>(cell.x == 1) +
                                   static_cast<int>(cell.y == 1) +
                                   static_cast<int>(cell.z == 1);
            secondarySignal +=
                ((cell.x + cell.y + cell.z + level + runtimeDepth_) & 1) == 0 ? 0.20f
                                                                                : 0.05f;
            if (middleAxes >= sponge.voidAxisThreshold) {
              carved = true;
              break;
            }

            local -= glm::floor(local);
          }

          const bool solid = sponge.invert ? carved : !carved;
          if (!solid) {
            continue;
          }

          BlockId value =
              secondarySignal > 0.45f ? sponge.palette.secondary
                                      : sponge.palette.primary;
          if (useAccentNoise) {
            const float accentNoise =
                seededFbm(baseX[x] * sponge.accentNoiseScale,
                          baseY[y] * sponge.accentNoiseScale,
                          baseZ[z] * sponge.accentNoiseScale, accentOffset, 2, 0.5f);
            if (accentNoise > sponge.accentThreshold) {
              value = sponge.palette.accent;
            } else {
              const float secondaryScale = sponge.accentNoiseScale * 0.5f;
              const float secondaryNoise =
                  seededFbm(baseX[x] * secondaryScale, baseY[y] * secondaryScale,
                            baseZ[z] * secondaryScale, secondaryOffset, 2, 0.55f);
              if (secondaryNoise > 0.18f) {
                value = sponge.palette.secondary;
              }
            }
          }

          writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
        }
      }
    }
    return;
  }

  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      const int worldY = wy0 + y;
      if (!sponge.infiniteY && (worldY < sponge.minY || worldY > sponge.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; z++) {
        glm::vec3 point(baseX[x], baseY[y], baseZ[z]);
        const float sx = point.x * sponge.warpScale;
        const float sy = point.y * sponge.warpScale;
        const float sz = point.z * sponge.warpScale;
        point.x += seededFbm(sx + 19.0f, sy, sz, warpOffsetX, 2, 0.5f) *
                   sponge.warpStrength;
        point.y += seededFbm(sx, sy + 19.0f, sz, warpOffsetY, 2, 0.5f) *
                   sponge.warpStrength;
        point.z += seededFbm(sx, sy, sz + 19.0f, warpOffsetZ, 2, 0.5f) *
                   sponge.warpStrength;

        glm::vec3 local(
            positiveMod(point.x, static_cast<float>(sponge.cellSize.x)) /
                static_cast<float>(sponge.cellSize.x),
            positiveMod(point.y, static_cast<float>(sponge.cellSize.y)) /
                static_cast<float>(sponge.cellSize.y),
            positiveMod(point.z, static_cast<float>(sponge.cellSize.z)) /
                static_cast<float>(sponge.cellSize.z));

        bool carved = false;
        float secondarySignal = 0.0f;
        for (int level = 0; level < sponge.iterations; level++) {
          local *= 3.0f;
          glm::ivec3 cell(static_cast<int>(std::floor(local.x)),
                          static_cast<int>(std::floor(local.y)),
                          static_cast<int>(std::floor(local.z)));
          cell = glm::clamp(cell, glm::ivec3(0), glm::ivec3(2));

          const int middleAxes = static_cast<int>(cell.x == 1) +
                                 static_cast<int>(cell.y == 1) +
                                 static_cast<int>(cell.z == 1);
          secondarySignal +=
              ((cell.x + cell.y + cell.z + level + runtimeDepth_) & 1) == 0 ? 0.20f
                                                                              : 0.05f;
          if (middleAxes >= sponge.voidAxisThreshold) {
            carved = true;
            break;
          }

          local -= glm::floor(local);
        }

        const bool solid = sponge.invert ? carved : !carved;
        if (!solid) {
          continue;
        }

        BlockId value =
            secondarySignal > 0.45f ? sponge.palette.secondary
                                    : sponge.palette.primary;
        if (useAccentNoise) {
          const float accentNoise =
              seededFbm(point.x * sponge.accentNoiseScale,
                        point.y * sponge.accentNoiseScale,
                        point.z * sponge.accentNoiseScale, accentOffset, 2, 0.5f);
          if (accentNoise > sponge.accentThreshold) {
            value = sponge.palette.accent;
          } else {
            const float secondaryNoise =
                seededFbm(point.x * sponge.accentNoiseScale * 0.5f,
                          point.y * sponge.accentNoiseScale * 0.5f,
                          point.z * sponge.accentNoiseScale * 0.5f, secondaryOffset,
                          2, 0.55f);
            if (secondaryNoise > 0.18f) {
              value = sponge.palette.secondary;
            }
          }
        }

        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

// Funcao: aplica 'applyCaveSystemLayer' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'module' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
#pragma endregion

#pragma region CaveSystemModule
void PresetModuleGeneratorSource::applyCaveSystemLayer(
    Chunk& chunk, const BiomeModule& module) const {
  // O sistema de cavernas percorre celulas recursivas ate achar um tunel ou camara que capture o voxel atual.
  const CaveSystemModule& caves = module.caveSystem;
  if (!chunkIntersectsVerticalRange(chunk, caves.infiniteY, caves.minY, caves.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useWarp = caves.warpStrength > 0.0f && caves.warpScale > 0.0f;
  const bool useAccentNoise = caves.accentNoiseScale > 0.0f;
  const bool useChambers = caves.chamberChance > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset warpOffsetX = makeSeedOffset(moduleSeed ^ 0x517cc1b7u);
  const SeedOffset warpOffsetY = makeSeedOffset(moduleSeed ^ 0x76517d4fu);
  const SeedOffset warpOffsetZ = makeSeedOffset(moduleSeed ^ 0x27d4eb2du);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x94d049bbu);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0x6c8e9cf5u);
  const glm::vec3 rootCellSize(static_cast<float>(caves.rootCellSize.x),
                               static_cast<float>(caves.rootCellSize.y),
                               static_cast<float>(caves.rootCellSize.z));
  const float inverseSubdivision =
      1.0f / static_cast<float>(caves.subdivisionFactor);
  const float secondaryRadiusSq =
      caves.tunnelRadius * caves.tunnelRadius * 1.6f * 1.6f;
  const float accentRadiusSq =
      caves.tunnelRadius * caves.tunnelRadius * 1.1f * 1.1f;
  std::array<std::uint32_t, 8> levelSeeds{};
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int level = 0; level < caves.recursionLevels; level++) {
    levelSeeds[level] =
        moduleSeed ^ (0x9e3779b9u * static_cast<std::uint32_t>(level + 1));
  }
  for (int x = 0; x < Chunk::SIZE; x++) {
    baseX[x] = static_cast<float>(wx0 + x - caves.offset.x) + 0.5f;
  }
  for (int y = 0; y < Chunk::SIZE; y++) {
    baseY[y] = static_cast<float>(wy0 + y - caves.offset.y) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; z++) {
    baseZ[z] = static_cast<float>(wz0 + z - caves.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      const int worldY = wy0 + y;
      if (!caves.infiniteY && (worldY < caves.minY || worldY > caves.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; z++) {
        glm::vec3 point(baseX[x], baseY[y], baseZ[z]);
        if (useWarp) {
          const float sx = point.x * caves.warpScale;
          const float sy = point.y * caves.warpScale;
          const float sz = point.z * caves.warpScale;
          point.x += seededFbm(sx + 41.0f, sy, sz, warpOffsetX, 2, 0.5f) *
                     caves.warpStrength;
          point.y += seededFbm(sx, sy + 41.0f, sz, warpOffsetY, 2, 0.5f) *
                     caves.warpStrength;
          point.z += seededFbm(sx, sy, sz + 41.0f, warpOffsetZ, 2, 0.5f) *
                     caves.warpStrength;
        }

        glm::vec3 cellSize = rootCellSize;
        float tunnelRadius = caves.tunnelRadius;
        bool carved = false;
        float nearestFeatureDistanceSq = std::numeric_limits<float>::max();

        for (int level = 0; level < caves.recursionLevels; level++) {
          if (cellSize.x < 2.0f || cellSize.y < 2.0f || cellSize.z < 2.0f ||
              tunnelRadius < 0.35f) {
            break;
          }

          glm::vec3 cellCoord(point.x / cellSize.x, point.y / cellSize.y,
                              point.z / cellSize.z);
          glm::ivec3 cellIndex(static_cast<int>(std::floor(cellCoord.x)),
                               static_cast<int>(std::floor(cellCoord.y)),
                               static_cast<int>(std::floor(cellCoord.z)));
          glm::vec3 local = cellCoord - glm::floor(cellCoord);

          std::uint32_t state =
              hash3i(cellIndex.x, cellIndex.y, cellIndex.z, levelSeeds[level]);
          const int axis = static_cast<int>(state % 3u);
          const auto jitterCenter = [&](std::uint32_t salt) {
            if (caves.centerJitter <= 0.0f) {
              return 0.5f;
            }
            return 0.5f +
                   (hash01(state ^ salt) * 2.0f - 1.0f) * caves.centerJitter;
          };

          glm::vec3 tunnelCenter(0.5f);
          tunnelCenter[(axis + 1) % 3] = jitterCenter(0x68bc21ebu);
          tunnelCenter[(axis + 2) % 3] = jitterCenter(0x02e5be93u);
          tunnelCenter[axis] = jitterCenter(0x7f4a7c15u);

          const glm::vec3 tunnelDelta = (local - tunnelCenter) * cellSize;
          float tunnelDistanceSq = 0.0f;
          if (axis == 0) {
            tunnelDistanceSq = tunnelDelta.y * tunnelDelta.y +
                               tunnelDelta.z * tunnelDelta.z;
          } else if (axis == 1) {
            tunnelDistanceSq = tunnelDelta.x * tunnelDelta.x +
                               tunnelDelta.z * tunnelDelta.z;
          } else {
            tunnelDistanceSq = tunnelDelta.x * tunnelDelta.x +
                               tunnelDelta.y * tunnelDelta.y;
          }
          nearestFeatureDistanceSq =
              std::min(nearestFeatureDistanceSq, tunnelDistanceSq);
          if (tunnelDistanceSq <= tunnelRadius * tunnelRadius) {
            carved = true;
            break;
          }

          if (useChambers && hash01(state ^ 0xb5297a4du) < caves.chamberChance) {
            glm::vec3 chamberCenter(jitterCenter(0x1b56c4e9u),
                                    jitterCenter(0x3c6ef372u),
                                    jitterCenter(0xa54ff53au));
            const glm::vec3 chamberDelta = (local - chamberCenter) * cellSize;
            const float chamberDistanceSq = glm::dot(chamberDelta, chamberDelta);
            nearestFeatureDistanceSq =
                std::min(nearestFeatureDistanceSq, chamberDistanceSq);
            const float chamberRadius = tunnelRadius * caves.chamberRadiusScale;
            if (chamberDistanceSq <= chamberRadius * chamberRadius) {
              carved = true;
              break;
            }
          }

          cellSize *= inverseSubdivision;
          tunnelRadius *= caves.radiusFalloff;
        }

        if (carved) {
          continue;
        }

        const float secondaryNoise =
            seededFbm(point.x * 0.018f, point.y * 0.018f, point.z * 0.018f,
                      secondaryOffset, 3, 0.55f);
        BlockId value =
            (nearestFeatureDistanceSq < secondaryRadiusSq ||
             secondaryNoise > 0.22f)
                ? caves.palette.secondary
                : caves.palette.primary;

        if (useAccentNoise) {
          const float accentNoise =
              seededFbm(point.x * caves.accentNoiseScale,
                        point.y * caves.accentNoiseScale,
                        point.z * caves.accentNoiseScale, accentOffset, 2, 0.5f);
          if (accentNoise > caves.accentThreshold &&
              nearestFeatureDistanceSq < accentRadiusSq) {
            value = caves.palette.accent;
          }
        }

        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyCellularNoiseLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const CellularNoiseModule& cellular = module.cellularNoise;
  const VolumeNoiseModuleSettings& noiseSettings = cellular.noise;
  if (!chunkIntersectsVerticalRange(chunk, noiseSettings.infiniteY,
                                    noiseSettings.minY, noiseSettings.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useSecondaryNoise = noiseSettings.secondaryNoiseScale > 0.0f;
  const bool useAccentNoise = noiseSettings.accentNoiseScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0x18a4c59du);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x6c8e9cf5u);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; ++x) {
    baseX[x] = static_cast<float>(wx0 + x - noiseSettings.offset.x) + 0.5f;
  }
  for (int y = 0; y < Chunk::SIZE; ++y) {
    baseY[y] = static_cast<float>(wy0 + y - noiseSettings.offset.y) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; ++z) {
    baseZ[z] = static_cast<float>(wz0 + z - noiseSettings.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!noiseSettings.infiniteY &&
          (worldY < noiseSettings.minY || worldY > noiseSettings.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; ++z) {
        const float signal = noise::cellularFbm(
            baseX[x] * noiseSettings.baseScale, baseY[y] * noiseSettings.baseScale,
            baseZ[z] * noiseSettings.baseScale, moduleSeed ^ 0xa511e9b3u,
            noiseSettings.octaves, noiseSettings.persistence,
            noiseSettings.lacunarity, cellular.jitter, cellular.distanceBlend);
        const float signalMargin =
            thresholdMargin(signal, noiseSettings.threshold, noiseSettings.invert);
        if (signalMargin <= 0.0f) {
          continue;
        }

        const float secondaryNoise =
            useSecondaryNoise
                ? seededFbm(baseX[x] * noiseSettings.secondaryNoiseScale,
                            baseY[y] * noiseSettings.secondaryNoiseScale,
                            baseZ[z] * noiseSettings.secondaryNoiseScale,
                            secondaryOffset, 2, 0.55f)
                : -1.0f;
        const float accentNoise =
            useAccentNoise
                ? seededFbm(baseX[x] * noiseSettings.accentNoiseScale,
                            baseY[y] * noiseSettings.accentNoiseScale,
                            baseZ[z] * noiseSettings.accentNoiseScale, accentOffset,
                            2, 0.50f)
                : -1.0f;

        const BlockId value = pickVolumeNoiseBlock(
            noiseSettings, signalMargin, secondaryNoise, accentNoise);
        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyFractalNoiseLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const FractalNoiseModule& fractal = module.fractalNoise;
  const VolumeNoiseModuleSettings& noiseSettings = fractal.noise;
  if (!chunkIntersectsVerticalRange(chunk, noiseSettings.infiniteY,
                                    noiseSettings.minY, noiseSettings.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useSecondaryNoise = noiseSettings.secondaryNoiseScale > 0.0f;
  const bool useAccentNoise = noiseSettings.accentNoiseScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset fractalOffset = makeSeedOffset(moduleSeed ^ 0x243f6a88u);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0x85a308d3u);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x13198a2eu);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; ++x) {
    baseX[x] = static_cast<float>(wx0 + x - noiseSettings.offset.x) + 0.5f;
  }
  for (int y = 0; y < Chunk::SIZE; ++y) {
    baseY[y] = static_cast<float>(wy0 + y - noiseSettings.offset.y) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; ++z) {
    baseZ[z] = static_cast<float>(wz0 + z - noiseSettings.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!noiseSettings.infiniteY &&
          (worldY < noiseSettings.minY || worldY > noiseSettings.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; ++z) {
        const float signal = seededFbm(
            baseX[x] * noiseSettings.baseScale, baseY[y] * noiseSettings.baseScale,
            baseZ[z] * noiseSettings.baseScale, fractalOffset,
            noiseSettings.octaves, noiseSettings.persistence,
            noiseSettings.lacunarity);
        const float signalMargin =
            thresholdMargin(signal, noiseSettings.threshold, noiseSettings.invert);
        if (signalMargin <= 0.0f) {
          continue;
        }

        const float secondaryNoise =
            useSecondaryNoise
                ? seededFbm(baseX[x] * noiseSettings.secondaryNoiseScale,
                            baseY[y] * noiseSettings.secondaryNoiseScale,
                            baseZ[z] * noiseSettings.secondaryNoiseScale,
                            secondaryOffset, 2, 0.55f)
                : -1.0f;
        const float accentNoise =
            useAccentNoise
                ? seededFbm(baseX[x] * noiseSettings.accentNoiseScale,
                            baseY[y] * noiseSettings.accentNoiseScale,
                            baseZ[z] * noiseSettings.accentNoiseScale, accentOffset,
                            2, 0.50f)
                : -1.0f;

        const BlockId value = pickVolumeNoiseBlock(
            noiseSettings, signalMargin, secondaryNoise, accentNoise);
        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyRidgedNoiseLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const RidgedNoiseModule& ridged = module.ridgedNoise;
  const VolumeNoiseModuleSettings& noiseSettings = ridged.noise;
  if (!chunkIntersectsVerticalRange(chunk, noiseSettings.infiniteY,
                                    noiseSettings.minY, noiseSettings.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useSecondaryNoise = noiseSettings.secondaryNoiseScale > 0.0f;
  const bool useAccentNoise = noiseSettings.accentNoiseScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset ridgedOffset = makeSeedOffset(moduleSeed ^ 0x517cc1b7u);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0x76517d4fu);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x27d4eb2du);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; ++x) {
    baseX[x] = static_cast<float>(wx0 + x - noiseSettings.offset.x) + 0.5f;
  }
  for (int y = 0; y < Chunk::SIZE; ++y) {
    baseY[y] = static_cast<float>(wy0 + y - noiseSettings.offset.y) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; ++z) {
    baseZ[z] = static_cast<float>(wz0 + z - noiseSettings.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!noiseSettings.infiniteY &&
          (worldY < noiseSettings.minY || worldY > noiseSettings.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; ++z) {
        const float signal = seededRidgedFbm(
            baseX[x] * noiseSettings.baseScale, baseY[y] * noiseSettings.baseScale,
            baseZ[z] * noiseSettings.baseScale, ridgedOffset,
            noiseSettings.octaves, noiseSettings.persistence,
            noiseSettings.lacunarity, ridged.ridgeSharpness);
        const float signalMargin =
            thresholdMargin(signal, noiseSettings.threshold, noiseSettings.invert);
        if (signalMargin <= 0.0f) {
          continue;
        }

        const float secondaryNoise =
            useSecondaryNoise
                ? seededFbm(baseX[x] * noiseSettings.secondaryNoiseScale,
                            baseY[y] * noiseSettings.secondaryNoiseScale,
                            baseZ[z] * noiseSettings.secondaryNoiseScale,
                            secondaryOffset, 2, 0.55f)
                : -1.0f;
        const float accentNoise =
            useAccentNoise
                ? seededFbm(baseX[x] * noiseSettings.accentNoiseScale,
                            baseY[y] * noiseSettings.accentNoiseScale,
                            baseZ[z] * noiseSettings.accentNoiseScale, accentOffset,
                            2, 0.50f)
                : -1.0f;

        const BlockId value = pickVolumeNoiseBlock(
            noiseSettings, signalMargin, secondaryNoise, accentNoise);
        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyDomainWarpedNoiseLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const DomainWarpedNoiseModule& warped = module.domainWarpedNoise;
  const VolumeNoiseModuleSettings& noiseSettings = warped.noise;
  if (!chunkIntersectsVerticalRange(chunk, noiseSettings.infiniteY,
                                    noiseSettings.minY, noiseSettings.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const bool useSecondaryNoise = noiseSettings.secondaryNoiseScale > 0.0f;
  const bool useAccentNoise = noiseSettings.accentNoiseScale > 0.0f;
  const bool useWarp = warped.warpStrength > 0.0f && warped.warpScale > 0.0f;

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id);
  const SeedOffset baseOffset = makeSeedOffset(moduleSeed ^ 0x94d049bbu);
  const SeedOffset warpOffsetX = makeSeedOffset(moduleSeed ^ 0x6c8e9cf5u);
  const SeedOffset warpOffsetY = makeSeedOffset(moduleSeed ^ 0x1b56c4e9u);
  const SeedOffset warpOffsetZ = makeSeedOffset(moduleSeed ^ 0x3c6ef372u);
  const SeedOffset secondaryOffset = makeSeedOffset(moduleSeed ^ 0xa54ff53au);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x510e527fu);
  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseY{};
  std::array<float, Chunk::SIZE> baseZ{};

  for (int x = 0; x < Chunk::SIZE; ++x) {
    baseX[x] = static_cast<float>(wx0 + x - noiseSettings.offset.x) + 0.5f;
  }
  for (int y = 0; y < Chunk::SIZE; ++y) {
    baseY[y] = static_cast<float>(wy0 + y - noiseSettings.offset.y) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; ++z) {
    baseZ[z] = static_cast<float>(wz0 + z - noiseSettings.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!noiseSettings.infiniteY &&
          (worldY < noiseSettings.minY || worldY > noiseSettings.maxY)) {
        continue;
      }

      for (int z = 0; z < Chunk::SIZE; ++z) {
        glm::vec3 point(baseX[x], baseY[y], baseZ[z]);
        if (useWarp) {
          const float warpX =
              seededFbm(point.x * warped.warpScale + 19.0f,
                        point.y * warped.warpScale,
                        point.z * warped.warpScale, warpOffsetX,
                        warped.warpOctaves, warped.warpPersistence,
                        warped.warpLacunarity);
          const float warpY =
              seededFbm(point.x * warped.warpScale,
                        point.y * warped.warpScale + 37.0f,
                        point.z * warped.warpScale, warpOffsetY,
                        warped.warpOctaves, warped.warpPersistence,
                        warped.warpLacunarity);
          const float warpZ =
              seededFbm(point.x * warped.warpScale,
                        point.y * warped.warpScale,
                        point.z * warped.warpScale + 53.0f, warpOffsetZ,
                        warped.warpOctaves, warped.warpPersistence,
                        warped.warpLacunarity);
          point.x += warpX * warped.warpStrength;
          point.y += warpY * warped.warpStrength;
          point.z += warpZ * warped.warpStrength;
        }

        const float signal = seededFbm(
            point.x * noiseSettings.baseScale, point.y * noiseSettings.baseScale,
            point.z * noiseSettings.baseScale, baseOffset, noiseSettings.octaves,
            noiseSettings.persistence, noiseSettings.lacunarity);
        const float signalMargin =
            thresholdMargin(signal, noiseSettings.threshold, noiseSettings.invert);
        if (signalMargin <= 0.0f) {
          continue;
        }

        const float secondaryNoise =
            useSecondaryNoise
                ? seededFbm(point.x * noiseSettings.secondaryNoiseScale,
                            point.y * noiseSettings.secondaryNoiseScale,
                            point.z * noiseSettings.secondaryNoiseScale,
                            secondaryOffset, 2, 0.55f)
                : -1.0f;
        const float accentNoise =
            useAccentNoise
                ? seededFbm(point.x * noiseSettings.accentNoiseScale,
                            point.y * noiseSettings.accentNoiseScale,
                            point.z * noiseSettings.accentNoiseScale,
                            accentOffset, 2, 0.50f)
                : -1.0f;

        const BlockId value = pickVolumeNoiseBlock(
            noiseSettings, signalMargin, secondaryNoise, accentNoise);
        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyTreeGeneratorLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const TreeGeneratorModule& tree = module.treeGenerator;
  if (tree.spawnOnBlocks.empty() || tree.density <= 0.0f) {
    return;
  }

  const glm::ivec3 chunkMin(chunk.chunkPos.x * Chunk::SIZE,
                            chunk.chunkPos.y * Chunk::SIZE,
                            chunk.chunkPos.z * Chunk::SIZE);
  const glm::ivec3 chunkMax = chunkMin + glm::ivec3(Chunk::SIZE - 1);
  const int horizontalReach = treeHorizontalReach(tree.treeType);
  const int verticalReach = treeVerticalReach(tree.treeType);
  const int cellSize = treeCellSize(tree.density, tree.pattern);
  const int jitter =
      tree.pattern == VoxPlacementPattern::RANDOM_SCATTER ? treeJitter(cellSize) : 0;
  const int baseMinY =
      tree.infiniteY ? (chunkMin.y - verticalReach)
                     : std::max(tree.minY, chunkMin.y - verticalReach);
  const int baseMaxY =
      tree.infiniteY ? chunkMax.y : std::min(tree.maxY, chunkMax.y);
  if (baseMaxY < baseMinY) {
    return;
  }

  const int cellMinX = floorDiv(chunkMin.x - horizontalReach - jitter, cellSize);
  const int cellMaxX = floorDiv(chunkMax.x + horizontalReach + jitter, cellSize);
  const int cellMinZ = floorDiv(chunkMin.z - horizontalReach - jitter, cellSize);
  const int cellMaxZ = floorDiv(chunkMax.z + horizontalReach + jitter, cellSize);

  std::unordered_map<ChunkCacheKey, Chunk, ChunkCacheKeyHasher> baseChunkCache;
  const auto sampleBaseBlockAt = [&](const glm::ivec3& worldPos) -> BlockId {
    const ChunkCacheKey key{floorDiv(worldPos.x, Chunk::SIZE),
                            floorDiv(worldPos.y, Chunk::SIZE),
                            floorDiv(worldPos.z, Chunk::SIZE)};
    auto iterator = baseChunkCache.find(key);
    if (iterator == baseChunkCache.end()) {
      iterator = baseChunkCache
                     .try_emplace(key, glm::ivec3(key.x, key.y, key.z))
                     .first;
      generateBaseChunk(iterator->second);
    }

    const int localX = positiveModInt(worldPos.x, Chunk::SIZE);
    const int localY = positiveModInt(worldPos.y, Chunk::SIZE);
    const int localZ = positiveModInt(worldPos.z, Chunk::SIZE);
    return iterator->second.blocks[localX][localY][localZ];
  };

  const auto stampWorldBlock = [&](const glm::ivec3& worldPos, BlockId value) {
    if (worldPos.x < chunkMin.x || worldPos.x > chunkMax.x || worldPos.y < chunkMin.y ||
        worldPos.y > chunkMax.y || worldPos.z < chunkMin.z || worldPos.z > chunkMax.z) {
      return;
    }

    const glm::ivec3 localPos = worldPos - chunkMin;
    writeLayerBlock(chunk.blocks[localPos.x][localPos.y][localPos.z], value,
                    module.blendMode);
  };

  const std::uint32_t moduleSeed = seed() ^ hashString(module.id) ^ 0x73a41f2du;
  for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
    for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
      std::uint32_t state = hash3i(cellX, runtimeDepth_, cellZ, moduleSeed);
      glm::ivec3 trunkBase(cellX * cellSize + (cellSize / 2), 0,
                           cellZ * cellSize + (cellSize / 2));
      if (tree.pattern == VoxPlacementPattern::RANDOM_SCATTER) {
        trunkBase.x += randRange(state, -jitter, jitter);
        trunkBase.z += randRange(state, -jitter, jitter);
      }

      const int clearanceHeight = verticalReach + 1;
      int foundBaseY = std::numeric_limits<int>::min();
      for (int y = baseMaxY; y >= baseMinY; --y) {
        const BlockId supportBlock =
            sampleBaseBlockAt(glm::ivec3(trunkBase.x, y - 1, trunkBase.z));
        if (!containsBlockType(tree.spawnOnBlocks, supportBlock)) {
          continue;
        }

        bool hasClearance = true;
        for (int dy = 0; dy < clearanceHeight; ++dy) {
          const BlockId occupiedBlock =
              sampleBaseBlockAt(glm::ivec3(trunkBase.x, y + dy, trunkBase.z));
          if (!isReplaceableBlock(occupiedBlock)) {
            hasClearance = false;
            break;
          }
        }

        if (!hasClearance) {
          continue;
        }

        foundBaseY = y;
        break;
      }

      if (foundBaseY == std::numeric_limits<int>::min()) {
        continue;
      }

      trunkBase.y = foundBaseY;
      std::uint32_t treeState =
          hash3i(trunkBase.x, trunkBase.y, trunkBase.z, moduleSeed ^ 0x9E3779B9u);

      if (tree.treeType == TreeGeneratorType::TRUNK_ONLY) {
        const int trunkHeight = randRange(treeState, 4, 8);
        for (int dy = 0; dy < trunkHeight; ++dy) {
          stampWorldBlock(glm::ivec3(trunkBase.x, trunkBase.y + dy, trunkBase.z),
                          tree.trunkBlock);
        }
        continue;
      }

      if (tree.treeType == TreeGeneratorType::STRANGE) {
        const int trunkHeight = randRange(treeState, 5, 9);
        int trunkX = trunkBase.x;
        int trunkZ = trunkBase.z;
        for (int dy = 0; dy < trunkHeight; ++dy) {
          if (dy > 1 && dy < trunkHeight - 1 && (dy % 2) == 0) {
            trunkX += randRange(treeState, -1, 1);
            trunkZ += randRange(treeState, -1, 1);
          }

          const glm::ivec3 trunkPos(trunkX, trunkBase.y + dy, trunkZ);
          stampWorldBlock(trunkPos, tree.trunkBlock);

          if (dy >= trunkHeight / 2 && dy < trunkHeight - 1 && (dy % 2) == 1) {
            const int branchLength = randRange(treeState, 1, 2);
            const int direction = randRange(treeState, 0, 3);
            glm::ivec3 branchDir(0);
            switch (direction) {
            case 0:
              branchDir.x = 1;
              break;
            case 1:
              branchDir.x = -1;
              break;
            case 2:
              branchDir.z = 1;
              break;
            default:
              branchDir.z = -1;
              break;
            }

            for (int step = 1; step <= branchLength; ++step) {
              stampWorldBlock(trunkPos + branchDir * step, tree.trunkBlock);
            }
          }
        }
        continue;
      }

      const int trunkHeight = randRange(treeState, 4, 7);
      for (int dy = 0; dy < trunkHeight; ++dy) {
        stampWorldBlock(glm::ivec3(trunkBase.x, trunkBase.y + dy, trunkBase.z),
                        tree.trunkBlock);
      }

      const int canopyBaseY = trunkBase.y + trunkHeight - 2;
      for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -1; dy <= 2; ++dy) {
          for (int dz = -2; dz <= 2; ++dz) {
            const int horizontalDistance = std::abs(dx) + std::abs(dz);
            if ((dy == -1 && horizontalDistance > 2) ||
                (dy == 2 && horizontalDistance > 1) ||
                (std::abs(dx) == 2 && std::abs(dz) == 2)) {
              continue;
            }
            if (dx == 0 && dz == 0 && dy <= 0) {
              continue;
            }

            stampWorldBlock(
                glm::ivec3(trunkBase.x + dx, canopyBaseY + dy, trunkBase.z + dz),
                tree.leavesBlock);
          }
        }
      }

      stampWorldBlock(glm::ivec3(trunkBase.x, canopyBaseY + 3, trunkBase.z),
                      tree.leavesBlock);
    }
  }
}

void PresetModuleGeneratorSource::applyFloatingIslandsLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const FloatingIslandsModule& islands = module.floatingIslands;
  if (!chunkIntersectsVerticalRange(chunk, islands.infiniteY, islands.minY,
                                    islands.maxY)) {
    return;
  }
  if (islands.spawnChance <= 0.0f) {
    return;
  }

  const glm::ivec3 chunkMin(chunk.chunkPos.x * Chunk::SIZE,
                            chunk.chunkPos.y * Chunk::SIZE,
                            chunk.chunkPos.z * Chunk::SIZE);
  const glm::ivec3 chunkMax = chunkMin + glm::ivec3(Chunk::SIZE - 1);
  const int maxRadius = std::max(islands.minRadius, islands.maxRadius);
  const int maxHeight = std::max(islands.minHeight, islands.maxHeight);
  const int horizontalReach =
      maxRadius + std::max(islands.jitter.x, islands.jitter.y) + 2;
  const int verticalReach =
      maxHeight + (islands.infiniteY ? islands.verticalJitter : 0) + 4;

  const int cellMinX =
      floorDiv(chunkMin.x - islands.offset.x - horizontalReach,
               islands.cellSize.x);
  const int cellMaxX =
      floorDiv(chunkMax.x - islands.offset.x + horizontalReach,
               islands.cellSize.x);
  const int cellMinZ =
      floorDiv(chunkMin.z - islands.offset.z - horizontalReach,
               islands.cellSize.y);
  const int cellMaxZ =
      floorDiv(chunkMax.z - islands.offset.z + horizontalReach,
               islands.cellSize.y);
  const int cellMinY =
      islands.infiniteY
          ? floorDiv(chunkMin.y - islands.offset.y - verticalReach,
                     islands.verticalSpacing)
          : 0;
  const int cellMaxY =
      islands.infiniteY
          ? floorDiv(chunkMax.y - islands.offset.y + verticalReach,
                     islands.verticalSpacing)
          : 0;

  const std::uint32_t moduleSeed =
      seed() ^ hashString(module.id) ^ 0x4cf5ad43u;
  const SeedOffset edgeOffset = makeSeedOffset(moduleSeed ^ 0xa511e9b3u);
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0x9e3779b9u);
  const bool useEdgeNoise =
      islands.edgeNoiseScale > 0.0f && islands.edgeNoiseStrength > 0.0f;
  const bool useAccentNoise = islands.accentNoiseScale > 0.0f;

  for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
    for (int cellY = cellMinY; cellY <= cellMaxY; ++cellY) {
      for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
        std::uint32_t state =
            hash3i(cellX, islands.infiniteY ? cellY : runtimeDepth_, cellZ,
                   moduleSeed);
        if (hash01(state) > islands.spawnChance) {
          continue;
        }

        glm::ivec3 center(islands.offset.x + cellX * islands.cellSize.x +
                              islands.cellSize.x / 2,
                          0,
                          islands.offset.z + cellZ * islands.cellSize.y +
                              islands.cellSize.y / 2);
        center.x += randRange(state, -islands.jitter.x, islands.jitter.x);
        center.z += randRange(state, -islands.jitter.y, islands.jitter.y);
        center.y = islands.infiniteY
                       ? islands.offset.y + cellY * islands.verticalSpacing +
                             islands.verticalSpacing / 2 +
                             randRange(state, -islands.verticalJitter,
                                       islands.verticalJitter)
                       : randRange(state, islands.minY, islands.maxY);

        const int radiusX = randRange(state, islands.minRadius, islands.maxRadius);
        const int radiusZ = randRange(state, islands.minRadius, islands.maxRadius);
        const int islandHeight =
            randRange(state, islands.minHeight, islands.maxHeight);

        const glm::ivec3 islandMin(center.x - radiusX - 2,
                                   center.y - islandHeight - 2,
                                   center.z - radiusZ - 2);
        const glm::ivec3 islandMax(center.x + radiusX + 2,
                                   center.y + islandHeight / 2 + 4,
                                   center.z + radiusZ + 2);
        if (islandMax.x < chunkMin.x || islandMin.x > chunkMax.x ||
            islandMax.y < chunkMin.y || islandMin.y > chunkMax.y ||
            islandMax.z < chunkMin.z || islandMin.z > chunkMax.z) {
          continue;
        }

        const int localMinX = std::max(0, islandMin.x - chunkMin.x);
        const int localMaxX =
            std::min(Chunk::SIZE - 1, islandMax.x - chunkMin.x);
        const int localMinY = std::max(0, islandMin.y - chunkMin.y);
        const int localMaxY =
            std::min(Chunk::SIZE - 1, islandMax.y - chunkMin.y);
        const int localMinZ = std::max(0, islandMin.z - chunkMin.z);
        const int localMaxZ =
            std::min(Chunk::SIZE - 1, islandMax.z - chunkMin.z);

        for (int x = localMinX; x <= localMaxX; ++x) {
          const float worldX = static_cast<float>(chunkMin.x + x) + 0.5f;
          const float nx =
              (worldX - static_cast<float>(center.x)) /
              static_cast<float>(std::max(radiusX, 1));
          for (int z = localMinZ; z <= localMaxZ; ++z) {
            const float worldZ = static_cast<float>(chunkMin.z + z) + 0.5f;
            const float nz =
                (worldZ - static_cast<float>(center.z)) /
                static_cast<float>(std::max(radiusZ, 1));
            const float radial = std::sqrt(nx * nx + nz * nz);

            float edgeNoise = 0.0f;
            if (useEdgeNoise) {
              edgeNoise =
                  seededFbm(worldX * islands.edgeNoiseScale,
                            static_cast<float>(center.y) * islands.edgeNoiseScale,
                            worldZ * islands.edgeNoiseScale, edgeOffset, 3, 0.55f);
            }
            const float edgeAllowance =
                std::clamp(1.0f + edgeNoise * islands.edgeNoiseStrength, 0.55f,
                           1.45f);
            if (radial > edgeAllowance) {
              continue;
            }

            const float normalizedRadial = radial / edgeAllowance;
            const float dome =
                std::clamp(1.0f - normalizedRadial * normalizedRadial, 0.0f,
                           1.0f);
            const float topY =
                static_cast<float>(center.y) + islands.surfaceThickness +
                dome * static_cast<float>(islandHeight) * 0.35f +
                edgeNoise * static_cast<float>(islandHeight) * 0.10f;
            const float bottomY =
                static_cast<float>(center.y) -
                (0.15f + std::pow(dome, islands.undersideSteepness)) *
                    static_cast<float>(islandHeight);

            for (int y = localMinY; y <= localMaxY; ++y) {
              const float worldY = static_cast<float>(chunkMin.y + y) + 0.5f;
              if (worldY < bottomY || worldY > topY) {
                continue;
              }

              const float surfaceDistance = topY - worldY;
              BlockId value =
                  surfaceDistance <= islands.surfaceThickness
                      ? islands.palette.secondary
                      : islands.palette.primary;
              if (useAccentNoise) {
                const float accentNoise =
                    seededFbm(worldX * islands.accentNoiseScale,
                              worldY * islands.accentNoiseScale,
                              worldZ * islands.accentNoiseScale, accentOffset, 2,
                              0.50f);
                if (accentNoise > islands.accentThreshold &&
                    surfaceDistance > islands.surfaceThickness * 0.5f) {
                  value = islands.palette.accent;
                }
              }

              writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
            }
          }
        }
      }
    }
  }
}

void PresetModuleGeneratorSource::applyOneBlockLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const OneBlockModule& one = module.oneBlock;
  if (one.density <= 0.0f) {
    return;
  }

  const glm::ivec3 chunkMin(chunk.chunkPos.x * Chunk::SIZE,
                            chunk.chunkPos.y * Chunk::SIZE,
                            chunk.chunkPos.z * Chunk::SIZE);
  const glm::ivec3 chunkMax = chunkMin + glm::ivec3(Chunk::SIZE - 1);
  if (!one.infiniteY &&
      (chunkMax.y < one.minY + 1 || chunkMin.y > one.maxY + one.maxBlocks)) {
    return;
  }

  const int jitterX =
      one.pattern == VoxPlacementPattern::RANDOM_SCATTER ? one.jitter.x : 0;
  const int jitterZ =
      one.pattern == VoxPlacementPattern::RANDOM_SCATTER ? one.jitter.y : 0;
  const int cellMinX = floorDiv(chunkMin.x - jitterX, one.cellSize.x);
  const int cellMaxX = floorDiv(chunkMax.x + jitterX, one.cellSize.x);
  const int cellMinZ = floorDiv(chunkMin.z - jitterZ, one.cellSize.y);
  const int cellMaxZ = floorDiv(chunkMax.z + jitterZ, one.cellSize.y);
  const int supportMinY =
      one.infiniteY ? chunkMin.y - one.maxBlocks
                    : std::max(one.minY, chunkMin.y - one.maxBlocks);
  const int supportMaxY =
      one.infiniteY ? chunkMax.y - 1 : std::min(one.maxY, chunkMax.y - 1);
  if (supportMaxY < supportMinY) {
    return;
  }

  std::unordered_map<ChunkCacheKey, Chunk, ChunkCacheKeyHasher> baseChunkCache;
  const auto sampleBaseBlockAt = [&](const glm::ivec3& worldPos) -> BlockId {
    const ChunkCacheKey key{floorDiv(worldPos.x, Chunk::SIZE),
                            floorDiv(worldPos.y, Chunk::SIZE),
                            floorDiv(worldPos.z, Chunk::SIZE)};
    auto iterator = baseChunkCache.find(key);
    if (iterator == baseChunkCache.end()) {
      iterator = baseChunkCache
                     .try_emplace(key, glm::ivec3(key.x, key.y, key.z))
                     .first;
      generateBaseChunk(iterator->second);
    }

    const int localX = positiveModInt(worldPos.x, Chunk::SIZE);
    const int localY = positiveModInt(worldPos.y, Chunk::SIZE);
    const int localZ = positiveModInt(worldPos.z, Chunk::SIZE);
    return iterator->second.blocks[localX][localY][localZ];
  };

  const auto stampWorldBlock = [&](const glm::ivec3& worldPos, BlockId value) {
    if (worldPos.x < chunkMin.x || worldPos.x > chunkMax.x ||
        worldPos.y < chunkMin.y || worldPos.y > chunkMax.y ||
        worldPos.z < chunkMin.z || worldPos.z > chunkMax.z) {
      return;
    }

    const glm::ivec3 localPos = worldPos - chunkMin;
    writeLayerBlock(chunk.blocks[localPos.x][localPos.y][localPos.z], value,
                    module.blendMode);
  };

  const std::uint32_t moduleSeed =
      seed() ^ hashString(module.id) ^ 0x2f6e2b1du;
  for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
    for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
      std::uint32_t state = hash3i(cellX, runtimeDepth_, cellZ, moduleSeed);
      if (hash01(state) > one.density) {
        continue;
      }

      glm::ivec3 column(cellX * one.cellSize.x + one.cellSize.x / 2, 0,
                        cellZ * one.cellSize.y + one.cellSize.y / 2);
      if (one.pattern == VoxPlacementPattern::RANDOM_SCATTER) {
        column.x += randRange(state, -jitterX, jitterX);
        column.z += randRange(state, -jitterZ, jitterZ);
      }

      int supportY = std::numeric_limits<int>::min();
      for (int y = supportMaxY; y >= supportMinY; --y) {
        if (sampleBaseBlockAt(glm::ivec3(column.x, y, column.z)) !=
            one.supportBlock) {
          continue;
        }
        if (one.requireAir &&
            !isReplaceableBlock(
                sampleBaseBlockAt(glm::ivec3(column.x, y + 1, column.z)))) {
          continue;
        }

        supportY = y;
        break;
      }
      if (supportY == std::numeric_limits<int>::min()) {
        continue;
      }

      std::uint32_t plantState =
          hash3i(column.x, supportY, column.z, moduleSeed ^ 0x9e3779b9u);
      const int blockCount = randRange(plantState, one.minBlocks, one.maxBlocks);

      bool hasClearance = true;
      if (one.requireAir) {
        for (int index = 1; index <= blockCount; ++index) {
          if (!isReplaceableBlock(sampleBaseBlockAt(
                  glm::ivec3(column.x, supportY + index, column.z)))) {
            hasClearance = false;
            break;
          }
        }
      }
      if (!hasClearance) {
        continue;
      }

      for (int index = 1; index <= blockCount; ++index) {
        const float accentRoll =
            hash01(hash3i(column.x, supportY + index, column.z,
                          moduleSeed ^ 0x68bc21ebu));
        const BlockId value =
            accentRoll < one.accentChance ? one.accentBlock : one.block;
        stampWorldBlock(glm::ivec3(column.x, supportY + index, column.z), value);
      }
    }
  }
}

void PresetModuleGeneratorSource::applyBackroomsLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const BackroomsModule& backrooms = module.backrooms;
  if (!chunkIntersectsVerticalRange(chunk, backrooms.infiniteY, backrooms.minY,
                                    backrooms.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const std::uint32_t moduleSeed =
      seed() ^ hashString(module.id) ^ 0x7f4a7c15u;
  const SeedOffset accentOffset = makeSeedOffset(moduleSeed ^ 0xa4093822u);
  const bool useAccentNoise = backrooms.accentNoiseScale > 0.0f;
  const int floorThickness =
      std::min(backrooms.floorThickness, backrooms.storyHeight - 2);
  const int ceilingThickness =
      std::min(backrooms.ceilingThickness,
               backrooms.storyHeight - floorThickness - 1);

  const auto passageOnXBoundary = [&](int boundaryX, int cellZ, int storyIndex,
                                      int localZ) {
    const std::uint32_t state =
        hash3i(boundaryX, storyIndex, cellZ, moduleSeed ^ 0x3c6ef372u);
    if (hash01(state) > backrooms.passageChance) {
      return false;
    }

    const int usableWidth =
        std::max(1, backrooms.cellSize.y - backrooms.wallThickness * 2 -
                        backrooms.passageWidth);
    const int doorStart =
        backrooms.wallThickness +
        static_cast<int>(hash01(state ^ 0x1b56c4e9u) *
                         static_cast<float>(usableWidth));
    return localZ >= doorStart && localZ < doorStart + backrooms.passageWidth;
  };

  const auto passageOnZBoundary = [&](int cellX, int boundaryZ, int storyIndex,
                                      int localX) {
    const std::uint32_t state =
        hash3i(cellX, storyIndex, boundaryZ, moduleSeed ^ 0x510e527fu);
    if (hash01(state) > backrooms.passageChance) {
      return false;
    }

    const int usableWidth =
        std::max(1, backrooms.cellSize.x - backrooms.wallThickness * 2 -
                        backrooms.passageWidth);
    const int doorStart =
        backrooms.wallThickness +
        static_cast<int>(hash01(state ^ 0x94d049bbu) *
                         static_cast<float>(usableWidth));
    return localX >= doorStart && localX < doorStart + backrooms.passageWidth;
  };

  for (int x = 0; x < Chunk::SIZE; ++x) {
    const int worldX = wx0 + x;
    const int cellSourceX = worldX - backrooms.offset.x;
    const int cellX = floorDiv(cellSourceX, backrooms.cellSize.x);
    const int localX = positiveModInt(cellSourceX, backrooms.cellSize.x);
    const bool nearWest = localX < backrooms.wallThickness;
    const bool nearEast =
        localX >= backrooms.cellSize.x - backrooms.wallThickness;

    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!backrooms.infiniteY &&
          (worldY < backrooms.minY || worldY > backrooms.maxY)) {
        continue;
      }

      const int storySourceY =
          worldY - (backrooms.infiniteY ? backrooms.offset.y
                                        : backrooms.minY + backrooms.offset.y);
      const int storyIndex = floorDiv(storySourceY, backrooms.storyHeight);
      const int localY = positiveModInt(storySourceY, backrooms.storyHeight);

      for (int z = 0; z < Chunk::SIZE; ++z) {
        const int worldZ = wz0 + z;
        const int cellSourceZ = worldZ - backrooms.offset.z;
        const int cellZ = floorDiv(cellSourceZ, backrooms.cellSize.y);
        const int localZ = positiveModInt(cellSourceZ, backrooms.cellSize.y);
        const bool nearNorth = localZ < backrooms.wallThickness;
        const bool nearSouth =
            localZ >= backrooms.cellSize.y - backrooms.wallThickness;

        BlockId value = BlockIds::AIR;
        if (localY < floorThickness ||
            localY >= backrooms.storyHeight - ceilingThickness) {
          value = backrooms.palette.secondary;

          const bool lightSlot =
              localY == backrooms.storyHeight - ceilingThickness &&
              localX == backrooms.cellSize.x / 2 &&
              localZ == backrooms.cellSize.y / 2;
          if (lightSlot &&
              hash01(hash3i(cellX, storyIndex, cellZ,
                            moduleSeed ^ 0x27d4eb2du)) < backrooms.lightChance) {
            value = backrooms.palette.accent;
          }
        } else {
          bool wall = false;
          if (nearWest &&
              !passageOnXBoundary(cellX, cellZ, storyIndex, localZ)) {
            wall = true;
          }
          if (nearEast &&
              !passageOnXBoundary(cellX + 1, cellZ, storyIndex, localZ)) {
            wall = true;
          }
          if (nearNorth &&
              !passageOnZBoundary(cellX, cellZ, storyIndex, localX)) {
            wall = true;
          }
          if (nearSouth &&
              !passageOnZBoundary(cellX, cellZ + 1, storyIndex, localX)) {
            wall = true;
          }

          if (wall) {
            value = backrooms.palette.primary;
            if (useAccentNoise) {
              const float accentNoise =
                  seededFbm(static_cast<float>(worldX) * backrooms.accentNoiseScale,
                            static_cast<float>(worldY) * backrooms.accentNoiseScale,
                            static_cast<float>(worldZ) * backrooms.accentNoiseScale,
                            accentOffset, 2, 0.50f);
              if (accentNoise > backrooms.accentThreshold) {
                value = backrooms.palette.accent;
              }
            }
          }
        }

        if (value != BlockIds::AIR || module.blendMode == LayerBlendMode::OVERWRITE_ALL) {
          writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
        }
      }
    }
  }
}

void PresetModuleGeneratorSource::applyMinecraftStyleLayer(
    Chunk& chunk, const BiomeModule& module) const {
  const MinecraftStyleModule& minecraft = module.minecraftStyle;
  if (!chunkIntersectsVerticalRange(chunk, minecraft.infiniteY, minecraft.minY,
                                    minecraft.maxY)) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;
  const int finiteHeight = std::max(16, minecraft.maxY - minecraft.minY + 1);
  const int worldHeight =
      minecraft.infiniteY ? minecraft.worldHeight : finiteHeight;
  const int minSurface =
      std::min(worldHeight - 2,
               minecraft.bedrockThickness + minecraft.soilDepth + 2);
  const int maxSurface = std::max(minSurface, worldHeight - 2);

  const std::uint32_t moduleSeed =
      seed() ^ hashString(module.id) ^ 0xb5297a4du;
  const SeedOffset terrainOffset = makeSeedOffset(moduleSeed ^ 0x243f6a88u);
  const SeedOffset detailOffset = makeSeedOffset(moduleSeed ^ 0x85a308d3u);
  const SeedOffset caveOffset = makeSeedOffset(moduleSeed ^ 0x13198a2eu);
  const SeedOffset caveWarpOffset = makeSeedOffset(moduleSeed ^ 0x299f31d0u);
  const SeedOffset oreOffset = makeSeedOffset(moduleSeed ^ 0x6c8e9cf5u);

  std::array<float, Chunk::SIZE> baseX{};
  std::array<float, Chunk::SIZE> baseZ{};
  std::array<std::array<int, Chunk::SIZE>, Chunk::SIZE> surfaceHeights{};
  for (int x = 0; x < Chunk::SIZE; ++x) {
    baseX[x] = static_cast<float>(wx0 + x - minecraft.offset.x) + 0.5f;
  }
  for (int z = 0; z < Chunk::SIZE; ++z) {
    baseZ[z] = static_cast<float>(wz0 + z - minecraft.offset.z) + 0.5f;
  }

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int z = 0; z < Chunk::SIZE; ++z) {
      const float terrain =
          seededFbm(baseX[x] * minecraft.terrainScale, 0.0f,
                    baseZ[z] * minecraft.terrainScale, terrainOffset,
                    minecraft.terrainOctaves, minecraft.terrainPersistence);
      const float detail =
          minecraft.detailScale > 0.0f
              ? seededFbm(baseX[x] * minecraft.detailScale, 31.0f,
                          baseZ[z] * minecraft.detailScale, detailOffset, 3,
                          0.50f)
              : 0.0f;
      const int localSurface =
          static_cast<int>(std::round(static_cast<float>(minecraft.baseHeight) +
                                      terrain *
                                          static_cast<float>(
                                              minecraft.heightAmplitude) +
                                      detail *
                                          static_cast<float>(
                                              minecraft.detailAmplitude)));
      surfaceHeights[x][z] = std::clamp(localSurface, minSurface, maxSurface);
    }
  }

  const auto writeMaybeAir = [&](BlockId& target, BlockId value) {
    if (value != BlockIds::AIR || module.blendMode == LayerBlendMode::OVERWRITE_ALL) {
      writeLayerBlock(target, value, module.blendMode);
    }
  };

  for (int x = 0; x < Chunk::SIZE; ++x) {
    for (int y = 0; y < Chunk::SIZE; ++y) {
      const int worldY = wy0 + y;
      if (!minecraft.infiniteY &&
          (worldY < minecraft.minY || worldY > minecraft.maxY)) {
        continue;
      }

      const int localY =
          minecraft.infiniteY
              ? positiveModInt(worldY - minecraft.offset.y, worldHeight)
              : worldY - minecraft.minY;
      if (localY < 0 || localY >= worldHeight) {
        continue;
      }

      const int sliceBaseY = worldY - localY;
      for (int z = 0; z < Chunk::SIZE; ++z) {
        const int surfaceY = sliceBaseY + surfaceHeights[x][z];
        if (worldY > surfaceY) {
          writeMaybeAir(chunk.blocks[x][y][z], BlockIds::AIR);
          continue;
        }

        const int depthBelowSurface = surfaceY - worldY;
        if (minecraft.cavesEnabled && minecraft.caveScale > 0.0f &&
            depthBelowSurface > minecraft.soilDepth &&
            localY > minecraft.bedrockThickness + 1) {
          glm::vec3 cavePoint(baseX[x], static_cast<float>(localY) + 0.5f,
                              baseZ[z]);
          if (minecraft.caveWarpScale > 0.0f && minecraft.caveWarpStrength > 0.0f) {
            const float warp =
                seededFbm(cavePoint.x * minecraft.caveWarpScale,
                          cavePoint.y * minecraft.caveWarpScale,
                          cavePoint.z * minecraft.caveWarpScale, caveWarpOffset,
                          2, 0.55f);
            cavePoint += glm::vec3(warp * minecraft.caveWarpStrength);
          }

          const float caveNoise =
              seededFbm(cavePoint.x * minecraft.caveScale,
                        cavePoint.y * minecraft.caveScale,
                        cavePoint.z * minecraft.caveScale, caveOffset, 3, 0.55f);
          if (caveNoise > minecraft.caveThreshold) {
            writeMaybeAir(chunk.blocks[x][y][z], BlockIds::AIR);
            continue;
          }
        }

        BlockId value = minecraft.palette.shell;
        const std::uint32_t bedrockHash =
            hash3i(wx0 + x, localY, wz0 + z, moduleSeed ^ 0x68bc21ebu);
        const int jaggedBedrock =
            minecraft.bedrockThickness +
            static_cast<int>(hash01(bedrockHash) * 2.0f);
        if (localY <= jaggedBedrock) {
          value = minecraft.palette.recess;
        } else if (depthBelowSurface == 0) {
          value = minecraft.palette.surfaceRib;
        } else if (depthBelowSurface <= minecraft.soilDepth) {
          value = minecraft.palette.surfacePatch;
        } else {
          const float oreNoise =
              minecraft.oreScale > 0.0f
                  ? seededFbm(baseX[x] * minecraft.oreScale,
                              static_cast<float>(localY) * minecraft.oreScale,
                              baseZ[z] * minecraft.oreScale, oreOffset, 2, 0.50f)
                  : -1.0f;
          if (oreNoise > minecraft.oreThreshold) {
            value = minecraft.palette.accent;
          } else {
            const float strata =
                std::sin(static_cast<float>(localY) * 0.13f +
                         static_cast<float>(runtimeDepth_) * 0.37f);
            value = (localY < minecraft.baseHeight / 2 || strata < -0.45f)
                        ? minecraft.palette.core
                        : minecraft.palette.shell;
          }
        }

        writeLayerBlock(chunk.blocks[x][y][z], value, module.blendMode);
      }
    }
  }
}

// Funcao: aplica 'applyImportVoxLayer' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'runtime' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
#pragma endregion

#pragma region ImportVoxModule
void PresetModuleGeneratorSource::applyImportVoxLayer(
    Chunk& chunk, const ImportVoxRuntime& runtime) const {
  // O import VOX decide intersecao por celula antes de estampar para nao varrer assets desnecessariamente em cada chunk.
  const ImportVoxFilesModule& module = runtime.module.importVoxFiles;
  if (!chunkIntersectsVerticalRange(chunk, module.infiniteY, module.minY, module.maxY)) {
    return;
  }
  if (runtime.files.empty()) {
    return;
  }

  const glm::ivec3 chunkMin(chunk.chunkPos.x * Chunk::SIZE, chunk.chunkPos.y * Chunk::SIZE,
                            chunk.chunkPos.z * Chunk::SIZE);
  const glm::ivec3 chunkMax = chunkMin + glm::ivec3(Chunk::SIZE - 1);

  const int maxFootprintX =
      std::max(runtime.maxStructureSize.x, runtime.maxStructureSize.z);
  const int maxFootprintZ = maxFootprintX;
  const int jitterX = module.pattern == VoxPlacementPattern::RANDOM_SCATTER
                          ? module.jitter.x
                          : 0;
  const int jitterZ = module.pattern == VoxPlacementPattern::RANDOM_SCATTER
                          ? module.jitter.y
                          : 0;

  const int cellMinX =
      floorDiv(chunkMin.x - maxFootprintX + 1 - jitterX, module.cellSize.x);
  const int cellMaxX = floorDiv(chunkMax.x + jitterX, module.cellSize.x);
  const int cellMinZ =
      floorDiv(chunkMin.z - maxFootprintZ + 1 - jitterZ, module.cellSize.y);
  const int cellMaxZ = floorDiv(chunkMax.z + jitterZ, module.cellSize.y);
  const int maxStructureHeight = std::max(runtime.maxStructureSize.y, 1);
  const int verticalStride = Chunk::SIZE;
  const int cellMinY =
      module.infiniteY
          ? floorDiv(chunkMin.y - maxStructureHeight + 1, verticalStride)
          : 0;
  const int cellMaxY = module.infiniteY ? floorDiv(chunkMax.y, verticalStride) : 0;

  for (int cellX = cellMinX; cellX <= cellMaxX; cellX++) {
    for (int cellZ = cellMinZ; cellZ <= cellMaxZ; cellZ++) {
      for (int cellY = cellMinY; cellY <= cellMaxY; cellY++) {
        std::uint32_t state =
            hash3i(cellX, module.infiniteY ? cellY : runtimeDepth_, cellZ,
                   seed() ^ runtime.moduleHash ^ 0xA511E9B3u);

        if (module.pattern == VoxPlacementPattern::RANDOM_SCATTER &&
            hash01(state) > module.spawnChance) {
          continue;
        }

        const std::string& file =
            runtime.files[state % static_cast<std::uint32_t>(runtime.files.size())];
        const VoxStructureData* structure =
            loadVoxStructure(
                file, module.colorMapping,
                module.colorMapping == VoxColorMapping::DEFAULT
                    ? std::optional<BlockId>(module.defaultVoxel)
                    : std::nullopt);
        if (!structure) {
          continue;
        }

        const int rotation = module.rotationMode == VoxRotationMode::RANDOM_90
                                 ? static_cast<int>((state >> 8) & 3u)
                                 : module.fixedRotation;
        const glm::ivec3 rotatedSize = rotatedVoxSizeY(structure->size, rotation);

        glm::ivec3 origin(cellX * module.cellSize.x,
                          module.infiniteY ? (cellY * verticalStride) : module.minY,
                          cellZ * module.cellSize.y);
        if (module.pattern == VoxPlacementPattern::RANDOM_SCATTER) {
          origin.x += randRange(state, -module.jitter.x, module.jitter.x);
          origin.z += randRange(state, -module.jitter.y, module.jitter.y);
          if (module.infiniteY) {
            const int yOffsetMax = std::max(Chunk::SIZE - rotatedSize.y, 0);
            origin.y += randRange(state, 0, yOffsetMax);
          } else {
            origin.y = randRange(state, module.minY, module.maxY);
          }
        }

        const glm::ivec3 stampMax = origin + rotatedSize - glm::ivec3(1);
        const bool intersects =
            !(stampMax.x < chunkMin.x || origin.x > chunkMax.x ||
              stampMax.y < chunkMin.y || origin.y > chunkMax.y ||
              stampMax.z < chunkMin.z || origin.z > chunkMax.z);
        if (!intersects) {
          continue;
        }

        stampStructureIntoChunk(chunk, *structure, origin, rotation,
                                runtime.module.blendMode);
      }
    }
  }
}

// Funcao: executa 'stampStructureIntoChunk' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk', 'structure', 'origin', 'rotation', 'blendMode' para encapsular esta etapa especifica do subsistema.
void PresetModuleGeneratorSource::stampStructureIntoChunk(
    Chunk& chunk, const VoxStructureData& structure, const glm::ivec3& origin,
    int rotation, LayerBlendMode blendMode) const {
  const glm::ivec3 base(chunk.chunkPos.x * Chunk::SIZE, chunk.chunkPos.y * Chunk::SIZE,
                        chunk.chunkPos.z * Chunk::SIZE);

  for (const VoxStructureVoxel& voxel : structure.voxels) {
    const glm::ivec3 rotated =
        rotateVoxPositionY(voxel.position, structure.size, rotation);
    const glm::ivec3 worldPos = origin + rotated;
    const glm::ivec3 localPos = worldPos - base;
    if (static_cast<unsigned>(localPos.x) >= Chunk::SIZE ||
        static_cast<unsigned>(localPos.y) >= Chunk::SIZE ||
        static_cast<unsigned>(localPos.z) >= Chunk::SIZE) {
      continue;
    }
    writeLayerBlock(chunk.blocks[localPos.x][localPos.y][localPos.z], voxel.block,
                    blendMode);
  }
}

// Funcao: executa 'pickPerlinBlockType' na geracao procedural baseada em presets.
// Detalhe: usa 'module', 'wx', 'wy', 'wz', 'densityValue' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'BlockId' com o resultado composto por esta chamada.
#pragma endregion

#pragma region PerlinMaterialSelection
BlockId PresetModuleGeneratorSource::pickPerlinBlockType(
    const PerlinTerrainModule& module, int wx, int wy, int wz,
    float densityValue) const {
  const SeedOffset regionOffset = makeSeedOffset(seed() ^ 0xA5317A4Du);
  const SeedOffset patchOffset = makeSeedOffset(seed() ^ 0x6C8E9CF5u);
  const SeedOffset accentOffset = makeSeedOffset(seed() ^ 0x9E3779B9u);

  return pickPerlinBlockType(module, wx, wy, wz, densityValue, regionOffset,
                             patchOffset, accentOffset);
}

BlockId PresetModuleGeneratorSource::pickPerlinBlockType(
    const PerlinTerrainModule& module, int wx, int wy, int wz,
    float densityValue, const SeedOffset& regionOffset,
    const SeedOffset& patchOffset, const SeedOffset& accentOffset) const {
  const float surfaceDepth =
      std::clamp((densityValue - module.density.densityThreshold) / 0.34f, 0.0f, 1.0f);
  const float exposedness = 1.0f - surfaceDepth;

  const float fx = static_cast<float>(wx);
  const float fy = static_cast<float>(wy);
  const float fz = static_cast<float>(wz);

  const float regionField =
      seededFbm(fx * 0.028f, fy * 0.028f, fz * 0.028f, regionOffset, 3, 0.50f);
  const float patchField =
      seededFbm(fx * 0.055f, fy * 0.055f, fz * 0.055f, patchOffset, 2, 0.58f);
  const float accentField =
      seededFbm(fx * 0.090f, fy * 0.090f, fz * 0.090f, accentOffset, 2, 0.48f);
  const float layerField =
      std::sin(fy * 0.11f + regionField * 2.4f + patchField * 1.8f);

  const float surfaceMix = regionField * 0.65f + layerField * 0.35f;
  const float patchMix = patchField * 0.70f + layerField * 0.30f;
  const float accentMix =
      accentField + surfaceDepth * 0.10f + regionField * 0.08f -
      exposedness * 0.12f;

  if (exposedness > 0.72f) {
    if (accentMix > 0.62f && patchMix > 0.12f) {
      return module.palette.accent;
    }
    return (surfaceMix > -0.04f) ? module.palette.surfacePatch
                                 : module.palette.surfaceRib;
  }

  if (exposedness > 0.42f) {
    if (accentMix > 0.58f && surfaceMix > -0.20f) {
      return module.palette.accent;
    }
    if (patchMix > 0.26f) {
      return module.palette.surfacePatch;
    }
    if (layerField > 0.30f) {
      return module.palette.surfaceRib;
    }
    return module.palette.shell;
  }

  if (surfaceDepth < 0.82f) {
    if (accentMix > 0.54f) {
      return module.palette.accent;
    }
    if (surfaceMix < -0.46f && patchMix < -0.08f) {
      return module.palette.recess;
    }
    if (layerField > 0.42f && patchMix > 0.10f) {
      return module.palette.shell;
    }
    return module.palette.core;
  }

  if (accentMix > 0.50f) {
    return module.palette.accent;
  }
  if (surfaceMix < -0.56f) {
    return module.palette.recess;
  }
  return module.palette.core;
}

BlockId PresetModuleGeneratorSource::pickVolumeNoiseBlock(
    const VolumeNoiseModuleSettings& settings, float signalMargin,
    float secondaryNoise, float accentNoise) const {
  BlockId value = settings.palette.primary;

  if (signalMargin > 0.24f ||
      (settings.secondaryNoiseScale > 0.0f &&
       secondaryNoise > settings.secondaryThreshold)) {
    value = settings.palette.secondary;
  }

  if (settings.accentNoiseScale > 0.0f) {
    const float accentGate =
        settings.accentThreshold - std::min(signalMargin * 0.20f, 0.12f);
    if (accentNoise > accentGate) {
      value = settings.palette.accent;
    }
  }

  return value;
}

// Funcao: executa 'carveSpawnBubble' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk' para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region SpawnAndHashUtilities
void PresetModuleGeneratorSource::carveSpawnBubble(Chunk& chunk) {
  // O spawn bubble e aplicado no fim para sempre vencer a composicao procedural e garantir espaco utilizavel.
  const glm::ivec3 spawnA(0, 0, 0);
  const glm::ivec3 spawnB(12, 24, 12);
  const int radiusA = 3;
  const int radiusB = 5;

  const glm::ivec3 base(chunk.chunkPos.x * Chunk::SIZE, chunk.chunkPos.y * Chunk::SIZE,
                        chunk.chunkPos.z * Chunk::SIZE);

  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      for (int z = 0; z < Chunk::SIZE; z++) {
        const glm::ivec3 worldPos = base + glm::ivec3(x, y, z);
        const auto inBubble = [&](const glm::ivec3& center, int radius) {
          const glm::ivec3 delta = worldPos - center;
          return (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) <=
                 (radius * radius);
        };

        if (inBubble(spawnA, radiusA) || inBubble(spawnB, radiusB)) {
          chunk.blocks[x][y][z] = BlockIds::AIR;
        }
      }
    }
  }
}

void PresetModuleGeneratorSource::applyTopDecorationBlocks(Chunk& chunk) const {
  const auto& topDecorationDefinitions =
      BlockRegistry::instance().topDecorationDefinitions();
  if (topDecorationDefinitions.empty()) {
    return;
  }

  const int wx0 = chunk.chunkPos.x * Chunk::SIZE;
  const int wy0 = chunk.chunkPos.y * Chunk::SIZE;
  const int wz0 = chunk.chunkPos.z * Chunk::SIZE;

  for (const BlockDefinition* definition : topDecorationDefinitions) {
    if (!definition || !definition->topDecoration.enabled) {
      continue;
    }

    const BlockTopDecorationRule& topDecoration = definition->topDecoration;
    const std::uint32_t decorationSeed =
        seed() ^ hashString(topDecoration.seedKey.empty()
                                ? definition->id
                                : topDecoration.seedKey);

    const int offsetY = topDecoration.verticalOffset;
    if (offsetY <= 0 || offsetY >= Chunk::SIZE) {
      continue;
    }

    for (int x = 0; x < Chunk::SIZE; ++x) {
      for (int y = 0; y < Chunk::SIZE - offsetY; ++y) {
        for (int z = 0; z < Chunk::SIZE; ++z) {
          if (chunk.blocks[x][y][z] != topDecoration.anchorBlockId) {
            continue;
          }
          if (chunk.blocks[x][y + offsetY][z] != topDecoration.requiredAboveBlockId) {
            continue;
          }

          const int wx = wx0 + x;
          const int wy = wy0 + y + offsetY;
          const int wz = wz0 + z;
          const float spawnRoll = hash01(hash3i(wx, wy, wz, decorationSeed));
          if (spawnRoll > topDecoration.spawnChance) {
            continue;
          }

          chunk.blocks[x][y + offsetY][z] = definition->idValue;
        }
      }
    }
  }
}

// Funcao: calcula 'hashChunkBlocks' na geracao procedural baseada em presets.
// Detalhe: centraliza a logica necessaria para produzir um identificador deterministico usado em cache, lookup ou seed.
// Retorno: devolve 'std::uint64_t' com o valor numerico calculado para a proxima decisao do pipeline.
std::uint64_t hashChunkBlocks(
    const BlockId blocks[Chunk::SIZE][Chunk::SIZE][Chunk::SIZE]) {
  constexpr std::uint64_t kOffset = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;

  std::uint64_t hash = kOffset;
  for (int x = 0; x < Chunk::SIZE; x++) {
    for (int y = 0; y < Chunk::SIZE; y++) {
      for (int z = 0; z < Chunk::SIZE; z++) {
        hash ^= static_cast<std::uint64_t>(blocks[x][y][z]);
        hash *= kPrime;
      }
    }
  }
  return hash;
}

// Funcao: calcula 'hashChunk' na geracao procedural baseada em presets.
// Detalhe: usa 'chunk' para produzir um identificador deterministico usado em cache, lookup ou seed.
// Retorno: devolve 'std::uint64_t' com o valor numerico calculado para a proxima decisao do pipeline.
std::uint64_t hashChunk(const Chunk& chunk) {
  return hashChunkBlocks(chunk.blocks);
}

} // namespace VoxelGame
#pragma endregion
