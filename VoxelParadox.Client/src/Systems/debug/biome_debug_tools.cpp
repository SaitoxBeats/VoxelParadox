// Arquivo: VoxelParadox.Client/src/Systems/debug/biome_debug_tools.cpp
// Papel: implementa "biome debug tools" dentro do subsistema "debug" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#include "biome_debug_tools.hpp"

#include <array>
#include <cstdio>
#include <limits>

// Solution dependencies
#include "engine/engine.hpp"

// Client
#include "player/player.hpp"
#include "world/biome_registry.hpp"
#include "world/fractal_world.hpp"
#include "world/world_stack.hpp"
#include "render/hud/hud_portal_info.hpp"

namespace DebugBiomeTools {
namespace {

constexpr int kSearchRadiusXZ = 12;
constexpr int kSearchRadiusY = 16;
constexpr int kPrimeRadiusChunks = 2;
constexpr int kRelocationRadiusBlocks = 150;
constexpr int kRelocationRadiusY = 24;
constexpr float kGroundProbeDistance = 0.08f;

// Funcao: verifica 'isPositionFree' nas ferramentas de debug de biome.
// Detalhe: usa 'world', 'eyePosition', 'radius', 'bodyHeight', 'eyeHeight' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool isPositionFree(FractalWorld& world, const glm::vec3& eyePosition, float radius,
                    float bodyHeight, float eyeHeight) {
  const glm::vec3 feetPosition = eyePosition - glm::vec3(0.0f, eyeHeight, 0.0f);
  const glm::vec3 minCorner = feetPosition + glm::vec3(-radius, 0.0f, -radius);
  const glm::vec3 maxCorner =
      feetPosition + glm::vec3(radius, bodyHeight, radius) - glm::vec3(0.0005f);
  const glm::ivec3 minB = glm::ivec3(glm::floor(minCorner));
  const glm::ivec3 maxB = glm::ivec3(glm::floor(maxCorner));

  for (int bx = minB.x; bx <= maxB.x; bx++) {
    for (int by = minB.y; by <= maxB.y; by++) {
      for (int bz = minB.z; bz <= maxB.z; bz++) {
        if (isSolid(world.getBlock(glm::ivec3(bx, by, bz)))) {
          return false;
        }
      }
    }
  }
  return true;
}

// Funcao: verifica 'isPositionFreeInLoadedArea' nas ferramentas de debug de biome.
// Detalhe: usa 'world', 'eyePosition', 'radius', 'bodyHeight', 'eyeHeight' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool isPositionFreeInLoadedArea(FractalWorld& world, const glm::vec3& eyePosition,
                                float radius, float bodyHeight, float eyeHeight) {
  const glm::vec3 feetPosition = eyePosition - glm::vec3(0.0f, eyeHeight, 0.0f);
  const glm::vec3 minCorner = feetPosition + glm::vec3(-radius, 0.0f, -radius);
  const glm::vec3 maxCorner =
      feetPosition + glm::vec3(radius, bodyHeight, radius) - glm::vec3(0.0005f);
  const glm::ivec3 minB = glm::ivec3(glm::floor(minCorner));
  const glm::ivec3 maxB = glm::ivec3(glm::floor(maxCorner));

  for (int bx = minB.x; bx <= maxB.x; bx++) {
    for (int by = minB.y; by <= maxB.y; by++) {
      for (int bz = minB.z; bz <= maxB.z; bz++) {
        const glm::ivec3 block(bx, by, bz);
        Chunk* chunk =
            world.getChunkIfLoaded(FractalWorld::worldToChunkPos(block));
        if (!chunk || !chunk->generated) {
          return false;
        }
        if (isSolid(world.getBlock(block))) {
          return false;
        }
      }
    }
  }
  return true;
}

bool hasGroundSupport(FractalWorld& world, const glm::vec3& eyePosition, float radius,
                      float eyeHeight) {
  const glm::vec3 feetPosition = eyePosition - glm::vec3(0.0f, eyeHeight, 0.0f);
  const float sampleRadius = glm::max(0.0f, radius - 0.05f);
  const std::array<glm::vec2, 5> samples = {
      glm::vec2(0.0f, 0.0f),
      glm::vec2(-sampleRadius, -sampleRadius),
      glm::vec2(-sampleRadius, sampleRadius),
      glm::vec2(sampleRadius, -sampleRadius),
      glm::vec2(sampleRadius, sampleRadius),
  };

  const int supportY = static_cast<int>(std::floor(feetPosition.y - kGroundProbeDistance));
  for (const glm::vec2& sampleOffset : samples) {
    const glm::ivec3 block(
        static_cast<int>(std::floor(eyePosition.x + sampleOffset.x)),
        supportY,
        static_cast<int>(std::floor(eyePosition.z + sampleOffset.y)));
    if (isSolid(world.getBlock(block))) {
      return true;
    }
  }

  return false;
}

// Funcao: executa 'freeNeighborCount' nas ferramentas de debug de biome.
// Detalhe: usa 'world', 'cell' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
int freeNeighborCount(FractalWorld& world, const glm::ivec3& cell) {
  int freeCount = 0;
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      for (int dz = -1; dz <= 1; dz++) {
        if (!isSolid(world.getBlock(cell + glm::ivec3(dx, dy, dz)))) {
          freeCount++;
        }
      }
    }
  }
  return freeCount;
}

// Funcao: executa 'resetPlayerTraversalState' nas ferramentas de debug de biome.
// Detalhe: usa 'player' para encapsular esta etapa especifica do subsistema.
void resetPlayerTraversalState(Player& player) {
  player.velocity = glm::vec3(0.0f);
  player.transition = PlayerTransition::NONE;
  player.transitionTimer = 0.0f;
  player.hasTarget = false;
  player.targetBlock = glm::ivec3(0);
  player.targetNormal = glm::ivec3(0);
  player.isBreakingBlock = false;
  player.breakingBlock = glm::ivec3(0);
  player.breakingBlockType = BlockIds::AIR;
  player.breakingTimer = 0.0f;
  player.breakingProgress = 0.0f;
  player.nestedPreview = Player::NestedPreviewPortal{};
}

// Funcao: aplica 'applySafePlayerLocation' nas ferramentas de debug de biome.
// Detalhe: usa 'player', 'newPosition' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
void applySafePlayerLocation(Player& player, const glm::vec3& newPosition) {
  player.camera.position = newPosition;
  player.velocity = glm::vec3(0.0f);
  player.hasTarget = false;
  player.isBreakingBlock = false;
  player.breakingProgress = 0.0f;
  player.breakingTimer = 0.0f;
}

// Funcao: procura 'findBestLoadedLocationAroundPoint' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'origin', 'playerRadius' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
glm::vec3 findBestLoadedLocationAroundPoint(WorldStack& worldStack,
                                            const glm::vec3& origin,
                                            const Player& player) {
  FractalWorld* world = worldStack.currentWorld();
  if (!world) {
    return origin;
  }

  const glm::ivec3 base = glm::ivec3(glm::floor(origin));
  glm::vec3 bestPosition = origin;
  float bestScore = -std::numeric_limits<float>::infinity();
  bool found = false;

  for (int dy = -kRelocationRadiusY; dy <= kRelocationRadiusY; dy++) {
    for (int dx = -kRelocationRadiusBlocks; dx <= kRelocationRadiusBlocks; dx++) {
      for (int dz = -kRelocationRadiusBlocks; dz <= kRelocationRadiusBlocks; dz++) {
        const int distance2 = dx * dx + dy * dy + dz * dz;
        if (distance2 > kRelocationRadiusBlocks * kRelocationRadiusBlocks) {
          continue;
        }

        const glm::ivec3 cell = base + glm::ivec3(dx, dy, dz);
        const glm::vec3 candidate(cell.x + 0.5f,
                                  cell.y + player.getStandingEyeHeight(),
                                  cell.z + 0.5f);
        if (!isPositionFreeInLoadedArea(*world, candidate, player.playerRadius,
                                        player.getStandingHeight(),
                                        player.getStandingEyeHeight()) ||
            !hasGroundSupport(*world, candidate, player.playerRadius,
                              player.getStandingEyeHeight())) {
          continue;
        }

        const float score =
            static_cast<float>(freeNeighborCount(*world, cell)) * 10.0f -
            static_cast<float>(distance2);
        if (!found || score > bestScore) {
          bestScore = score;
          bestPosition = candidate;
          found = true;
        }
      }
    }
  }

  return found ? bestPosition : origin;
}

} // namespace

// Funcao: procura 'findBestSpawnLocation' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'spawnAnchor', 'playerRadius' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
glm::vec3 findBestSpawnLocation(WorldStack& worldStack, const glm::vec3& spawnAnchor,
                                const Player& player) {
  FractalWorld* world = worldStack.currentWorld();
  if (!world) {
    return spawnAnchor;
  }

  world->primeImmediateArea(spawnAnchor, kPrimeRadiusChunks);

  const glm::ivec3 base = glm::ivec3(glm::floor(spawnAnchor));
  glm::vec3 bestPosition = spawnAnchor;
  float bestScore = -std::numeric_limits<float>::infinity();
  bool found = false;

  for (int dy = -kSearchRadiusY; dy <= kSearchRadiusY; dy++) {
    for (int dx = -kSearchRadiusXZ; dx <= kSearchRadiusXZ; dx++) {
      for (int dz = -kSearchRadiusXZ; dz <= kSearchRadiusXZ; dz++) {
        const int horizontalDist2 = dx * dx + dz * dz;
        if (horizontalDist2 > kSearchRadiusXZ * kSearchRadiusXZ) {
          continue;
        }

        const glm::ivec3 cell = base + glm::ivec3(dx, dy, dz);
        const glm::vec3 candidate(cell.x + 0.5f,
                                  cell.y + player.getStandingEyeHeight(),
                                  cell.z + 0.5f);
        if (!isPositionFree(*world, candidate, player.playerRadius,
                            player.getStandingHeight(),
                            player.getStandingEyeHeight()) ||
            !hasGroundSupport(*world, candidate, player.playerRadius,
                              player.getStandingEyeHeight())) {
          continue;
        }

        const float distancePenalty =
            static_cast<float>(horizontalDist2) + static_cast<float>(dy * dy) * 0.35f;
        const float score =
            static_cast<float>(freeNeighborCount(*world, cell)) * 10.0f - distancePenalty;
        if (!found || score > bestScore) {
          bestScore = score;
          bestPosition = candidate;
          found = true;
        }
      }
    }
  }

  return found ? bestPosition : spawnAnchor;
}

// Funcao: atualiza 'updatePlayerLocationAroundPlayer' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'player' para sincronizar o estado derivado com o frame atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool updatePlayerLocationAroundPlayer(WorldStack& worldStack, Player& player) {
  FractalWorld* world = worldStack.currentWorld();
  if (!world) {
    return false;
  }

  const glm::vec3 searchOrigin = player.camera.position;
  const glm::vec3 bestLocation =
      findBestLoadedLocationAroundPoint(worldStack, searchOrigin, player);
  const glm::vec3 relocationDelta = bestLocation - searchOrigin;
  if (glm::dot(relocationDelta, relocationDelta) < 0.0001f) {
    std::printf("[DEBUG] Update location failed | Biome: %s | No free loaded area within %d blocks\n",
                worldStack.currentBiomeName().c_str(), kRelocationRadiusBlocks);
    return false;
  }

  applySafePlayerLocation(player, bestLocation);

  std::printf("[DEBUG] Updated player location | Biome: %s | Pos: %.2f, %.2f, %.2f | Radius: %d\n",
              worldStack.currentBiomeName().c_str(), bestLocation.x, bestLocation.y,
              bestLocation.z, kRelocationRadiusBlocks);
  return true;
}

// Funcao: executa 'teleportToBiome' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'player', 'portalInfo', 'biomeSelection', 'rootSeed', 'spawnAnchor', 'currentTime', 'lastTime' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool teleportToBiome(WorldStack& worldStack, Player& player, hudPortalInfo* portalInfo,
                     const BiomeSelection& biomeSelection, std::uint32_t rootSeed,
                     const glm::vec3& spawnAnchor, double currentTime,
                     double& lastTime) {
  if (portalInfo) {
    portalInfo->cancelEditing();
  }

  std::shared_ptr<const VoxelGame::BiomePreset> preset;
  if (biomeSelection.isPreset()) {
    preset = BiomeRegistry::instance().findPreset(biomeSelection.presetId);
    if (!preset) {
      std::printf("[BiomePreset] Teleport failed | preset '%s' is not loaded\n",
                  biomeSelection.presetId.c_str());
      return false;
    }
  }

  worldStack.init(rootSeed, biomeSelection, preset);
  resetPlayerTraversalState(player);
  player.clearHotbar();
  applySafePlayerLocation(
      player, findBestSpawnLocation(worldStack, spawnAnchor, player));

  lastTime = currentTime;
  ENGINE::INIT(currentTime);

  std::printf("[DEBUG] Reloaded world | Biome: %s | Spawn: %.2f, %.2f, %.2f\n",
              worldStack.currentBiomeName().c_str(), player.camera.position.x,
              player.camera.position.y, player.camera.position.z);
  return true;
}

} // namespace DebugBiomeTools
