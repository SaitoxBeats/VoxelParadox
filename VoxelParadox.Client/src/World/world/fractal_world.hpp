// Arquivo: VoxelParadox.Client/src/World/world/fractal_world.hpp
// Papel: declara "fractal world" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Solution dependencies
#include "engine/engine.hpp"
#include "engine/thread_pool.hpp"
#include "engine/visibility/cluster_occlusion.hpp"

// External
#include <glm/glm.hpp>

// Client
#include "biome.hpp"
#include "biome_preset.hpp"
#include "chunk.hpp"
#include "enemies/enemy_definition.hpp"
#include "gameplay/gameplay_status.hpp"
#include "items/item_catalog.hpp"
#include "world_generator.hpp"
#pragma endregion

#pragma region FractalWorldSupportTypes
struct IVec3Hash {
    // Funcao: executa 'operator' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'size_t' com o valor numerico calculado para a proxima decisao do pipeline.
    size_t operator()(const glm::ivec3& v) const {
        size_t h = 0;
        h ^= std::hash<int>()(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct IVec3Equal {
    // Funcao: executa 'operator' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

inline ThreadPool chunkThreadPool;

// ChunkData: An isolated container for raw generation bytes.
// By forcing background threads to return primitive structs instead of
// manipulating live OpenGL `Chunk` pointers, we prevent fatal Use-After-Free Context crashes
// when the player suddenly Ascends and the parent FractalWorld memory is instantly vaporized.
struct ChunkData {
    glm::ivec3 pos;
    BlockId blocks[16][16][16];
};

struct AsyncChunkState {
    struct FailedChunk {
        glm::ivec3 pos{0};
        std::string error;
    };

    std::mutex readyMutex;
    std::vector<std::unique_ptr<ChunkData>> readyChunks;
    std::vector<FailedChunk> failedChunks;
    std::atomic<bool> acceptingResults{true};
    std::atomic<int> queuedChunkGenerations{0};
};

// FractalWorld: Represents a single active physical dimension.
// All chunks out of bounds are unloaded aggressively.
#pragma endregion

#pragma region FractalWorldApi
class FractalWorld {
#pragma endregion

#pragma region FractalWorldLifecycleAndStreaming
public:
    struct RuntimePerformanceToggles {
        bool greedyMeshingEnabled = true;
    };

    struct RenderDiagnostics {
        int candidateChunkCount = 0;
        int frustumCulledChunkCount = 0;
        int occlusionCulledChunkCount = 0;
        int visibleClusterCount = 0;
        int visibleChunkCount = 0;
        int totalSubmittedVertices = 0;
    };

    static const RuntimePerformanceToggles& getRuntimePerformanceToggles() {
        return runtimePerformanceToggles();
    }

    static bool isGreedyMeshingEnabled() {
        return runtimePerformanceToggles().greedyMeshingEnabled;
    }

    static void setGreedyMeshingEnabled(bool enabled) {
        runtimePerformanceToggles().greedyMeshingEnabled = enabled;
    }

    const RenderDiagnostics& getLastRenderDiagnostics() const {
        return lastRenderDiagnostics;
    }

    void markAllChunksDirty() {
        for (const auto& [pos, chunk] : chunks) {
            if (chunk && chunk->generated) {
                markDirty(pos);
            }
        }
    }

    struct DroppedItem {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        InventoryItem item{};
        float spinPhase = 0.0f;
        bool attracting = false;
        float pickupDelaySeconds = 0.0f;
    };

    struct EnemySpawnRuntimeState {
        float cooldownSeconds = -1.0f;
    };

    int depth;
    uint32_t seed;
    BiomeSelection biomeSelection;
    std::shared_ptr<const VoxelGame::BiomePreset> biomePreset;
    int renderDistance = 5;
    int defaultRenderDistance = 5;
    WorldGenerator generator;
    std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>, IVec3Hash, IVec3Equal> chunks;

    std::unordered_map<glm::ivec3, BlockId, IVec3Hash, IVec3Equal> modifications;
    std::unordered_map<glm::ivec3, uint32_t, IVec3Hash, IVec3Equal> portalBlocks;
    std::unordered_map<glm::ivec3, BiomeSelection, IVec3Hash, IVec3Equal> portalBiomeSelections;
    std::vector<DroppedItem> droppedItems;
    std::vector<WorldEnemy> enemies;
    EnemySpawnRuntimeState enemySpawnState{};

    std::shared_ptr<AsyncChunkState> asyncChunkState;
    int maxQueuedChunkGenerations = 0;
    int maxReadyChunksPerFrame = 0;
    int maxMeshesPerFrame = 0;
    int memoryPressureCooldownFrames = 0;
    bool memoryPressureLogged = false;

    struct ChunkRetryState {
        int cooldownFrames = 0;
        bool remeshOnRelease = false;
    };

    static constexpr int kMemoryPressureCooldownFrames = 180;
    static constexpr int kChunkRetryCooldownFrames = 120;

    // Funcao: executa 'markSparseEditIndexDirty' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    void markSparseEditIndexDirty() {
        sparseEditIndexDirty = true;
    }

    // Funcao: executa 'rebuildSparseEditIndex' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    void rebuildSparseEditIndex() {
        chunkModifications.clear();
        chunkPortals.clear();

        for (const auto& [wp, bt] : modifications) {
            chunkModifications[worldToChunkPos(wp)].push_back({wp, bt});
        }
        for (const auto& [wp, sid] : portalBlocks) {
            (void)sid;
            chunkPortals[worldToChunkPos(wp)].push_back(wp);
        }

        sparseEditIndexDirty = false;
    }

    // Funcao: executa 'FractalWorld' no mundo fractal ativo.
    // Detalhe: usa 'depth', 'seed', 'biomeSelection', 'biomePreset' para encapsular esta etapa especifica do subsistema.
    FractalWorld(int depth, uint32_t seed, const BiomeSelection& biomeSelection,
                 std::shared_ptr<const VoxelGame::BiomePreset> biomePreset = nullptr)
        : depth(depth),
          seed(seed),
          biomeSelection(biomeSelection),
          biomePreset(std::move(biomePreset)),
          generator(depth, seed, this->biomePreset) {
        asyncChunkState = std::make_shared<AsyncChunkState>();

        renderDistance = this->biomePreset
                             ? std::clamp(this->biomePreset->preview.streamRenderDistance,
                                          2, 10)
                             : 5;
        defaultRenderDistance = renderDistance;
        refreshStreamingBudgets();
    }

    // Funcao: executa 'usesCustomBiomePreset' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool usesCustomBiomePreset() const { return biomePreset != nullptr; }

    // Funcao: libera '~FractalWorld' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
    ~FractalWorld() {
        if (!asyncChunkState) return;

        asyncChunkState->acceptingResults.store(false, std::memory_order_release);
        while (asyncChunkState->queuedChunkGenerations.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }

        std::lock_guard<std::mutex> lock(asyncChunkState->readyMutex);
        asyncChunkState->readyChunks.clear();
    }

    // Funcao: atualiza 'update' no mundo fractal ativo.
    // Detalhe: usa 'playerPos', 'viewForward' para sincronizar o estado derivado com o frame atual.
    void update(const glm::vec3& playerPos, const glm::vec3& viewForward) {
        // The world update is split into streaming, job integration, unload, and remesh stages.
        try {
            updateMemoryPressureState();
            refreshStreamingBudgets();
            glm::ivec3 center = worldToChunkPos(glm::ivec3(glm::floor(playerPos)));
            const glm::vec3 prioritizedViewForward = sanitizeViewForward(viewForward);
            loadChunksAround(center, prioritizedViewForward);
            processReadyChunks(center, prioritizedViewForward);
            unloadDistantChunks(center);
            rebuildDirtyMeshes(center, prioritizedViewForward);
        } catch (const std::bad_alloc&) {
            noteMemoryPressure("world update");
        }
    }

    // Funcao: verifica 'isAreaGenerated' no mundo fractal ativo.
    // Detalhe: usa 'playerPos', 'radiusChunks' para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool isAreaGenerated(const glm::vec3& playerPos, int radiusChunks = 1) const {
        const glm::ivec3 center = worldToChunkPos(glm::ivec3(glm::floor(playerPos)));
        for (int x = -radiusChunks; x <= radiusChunks; x++)
        for (int y = -radiusChunks; y <= radiusChunks; y++)
        for (int z = -radiusChunks; z <= radiusChunks; z++) {
            const glm::ivec3 cp = center + glm::ivec3(x, y, z);
            auto it = chunks.find(cp);
            if (it == chunks.end() || !it->second || !it->second->generated) {
                return false;
            }
        }
        return true;
    }

    // Funcao: executa 'primeImmediateArea' no mundo fractal ativo.
    // Detalhe: usa 'playerPos', 'visibleRadius' para encapsular esta etapa especifica do subsistema.
    void primeImmediateArea(const glm::vec3& playerPos, int visibleRadius = 1) {
        const glm::ivec3 center = worldToChunkPos(glm::ivec3(glm::floor(playerPos)));
        const int shellRadius = visibleRadius + 1;

        for (int x = -shellRadius; x <= shellRadius; x++)
        for (int y = -shellRadius; y <= shellRadius; y++)
        for (int z = -shellRadius; z <= shellRadius; z++) {
            getChunkBlocking(center + glm::ivec3(x, y, z));
        }

        for (int x = -visibleRadius; x <= visibleRadius; x++)
        for (int y = -visibleRadius; y <= visibleRadius; y++)
        for (int z = -visibleRadius; z <= visibleRadius; z++) {
            const glm::ivec3 cp = center + glm::ivec3(x, y, z);
            Chunk* chunk = getChunkIfLoaded(cp);
            if (!chunk || !chunk->generated) continue;

            Chunk* nx = getChunkIfLoaded(cp + glm::ivec3(-1, 0, 0));
            Chunk* px = getChunkIfLoaded(cp + glm::ivec3(1, 0, 0));
            Chunk* ny = getChunkIfLoaded(cp + glm::ivec3(0, -1, 0));
            Chunk* py = getChunkIfLoaded(cp + glm::ivec3(0, 1, 0));
            Chunk* nz = getChunkIfLoaded(cp + glm::ivec3(0, 0, -1));
            Chunk* pz = getChunkIfLoaded(cp + glm::ivec3(0, 0, 1));
            try {
                chunk->buildMesh(depth, nx, px, ny, py, nz, pz,
                                 isGreedyMeshingEnabled());
            } catch (const std::bad_alloc&) {
                noteMemoryPressure("immediate mesh build", &cp);
                scheduleChunkRetry(cp, true);
                return;
            }
        }
    }

    // Funcao: executa 'prepareReturnShell' no mundo fractal ativo.
    // Detalhe: usa 'focalPoints', 'visibleRadius', 'keepRadius' para encapsular esta etapa especifica do subsistema.
    void prepareReturnShell(const std::vector<glm::vec3>& focalPoints,
                            int visibleRadius = 1, int keepRadius = 2) {
        if (focalPoints.empty()) return;

        std::vector<glm::ivec3> centers;
        centers.reserve(focalPoints.size());
        for (const glm::vec3& point : focalPoints) {
            centers.push_back(worldToChunkPos(glm::ivec3(glm::floor(point))));
            primeImmediateArea(point, visibleRadius);
        }

        std::vector<glm::ivec3> toRemove;
        for (const auto& [pos, chunk] : chunks) {
            bool keep = false;
            for (const glm::ivec3& center : centers) {
                const glm::ivec3 delta = pos - center;
                if (std::abs(delta.x) <= keepRadius &&
                    std::abs(delta.y) <= keepRadius &&
                    std::abs(delta.z) <= keepRadius) {
                    keep = true;
                    break;
                }
            }

            if (!keep) {
                toRemove.push_back(pos);
            }
        }

        for (const glm::ivec3& pos : toRemove) {
            removeChunkOccupancy(pos);
            chunks.erase(pos);
        }
    }

    // Funcao: executa 'processReadyChunks' no mundo fractal ativo.
    // Detalhe: usa 'center', 'viewForward' para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region FractalWorldAsyncAndQueries
    void pruneRedundantSparseEditsForChunk(
        const glm::ivec3& chunkPos,
        const BlockId blocks[Chunk::SIZE][Chunk::SIZE][Chunk::SIZE]) {
        // Procedural decoration such as membrane_wire is deterministic from the seed.
        // Persist only the player's edits, not the untouched generated result.
        if (sparseEditIndexDirty) {
            rebuildSparseEditIndex();
        }

        const auto chunkEditIt = chunkModifications.find(chunkPos);
        if (chunkEditIt == chunkModifications.end()) {
            return;
        }

        bool removedAny = false;
        for (const auto& [worldPos, modifiedType] : chunkEditIt->second) {
            const glm::ivec3 localPos = worldToLocalPos(worldPos);
            const BlockId generatedType =
                blocks[localPos.x][localPos.y][localPos.z];
            if (generatedType != modifiedType) {
                continue;
            }

            auto modificationIt = modifications.find(worldPos);
            if (modificationIt == modifications.end()) {
                continue;
            }

            modifications.erase(modificationIt);
            removedAny = true;
        }

        if (removedAny) {
            sparseEditIndexDirty = true;
        }
    }

    void processReadyChunks(glm::ivec3 center, const glm::vec3& viewForward) {
        // Resultados prontos entram aqui para que a thread de geracao nunca toque em `Chunk` com recursos OpenGL vivos.
        std::vector<std::unique_ptr<ChunkData>> localReady;
        std::vector<AsyncChunkState::FailedChunk> localFailures;
        if (!asyncChunkState) return;
        {
            std::lock_guard<std::mutex> lock(asyncChunkState->readyMutex);
            if (asyncChunkState->readyChunks.empty() &&
                asyncChunkState->failedChunks.empty()) {
                return;
            }
            localReady = std::move(asyncChunkState->readyChunks);
            asyncChunkState->readyChunks.clear();
            localFailures = std::move(asyncChunkState->failedChunks);
            asyncChunkState->failedChunks.clear();
        }

        for (const auto& failure : localFailures) {
            auto it = chunks.find(failure.pos);
            if (it != chunks.end()) {
                removeChunkOccupancy(failure.pos);
                chunks.erase(it);
            }

            if (isAllocationFailureMessage(failure.error)) {
                noteMemoryPressure("chunk generation result", &failure.pos);
                scheduleChunkRetry(failure.pos, false);
            }

            if (!failure.error.empty()) {
                std::printf("[World][WARN] Chunk generation failed at (%d, %d, %d): %s\n",
                            failure.pos.x, failure.pos.y, failure.pos.z,
                            failure.error.c_str());
                std::fflush(stdout);
            }
        }

        localReady.erase(
            std::remove_if(localReady.begin(), localReady.end(),
                           [](const std::unique_ptr<ChunkData>& data) {
                               return data == nullptr;
                           }),
            localReady.end());
        if (localReady.empty()) {
            return;
        }

        std::sort(localReady.begin(), localReady.end(),
                  [this, center, &viewForward](const std::unique_ptr<ChunkData>& a,
                                               const std::unique_ptr<ChunkData>& b) {
                      if (!a || !b) {
                          return static_cast<bool>(a);
                      }
                      return prefersChunkOffset(a->pos - center, b->pos - center,
                                                viewForward);
                  });

        std::vector<std::unique_ptr<ChunkData>> deferredReady;
        if (localReady.size() > static_cast<size_t>(maxReadyChunksPerFrame)) {
            deferredReady.reserve(localReady.size() -
                                  static_cast<size_t>(maxReadyChunksPerFrame));
        }

        int integratedCount = 0;
        for (auto& data : localReady) {
            if (integratedCount >= maxReadyChunksPerFrame) {
                deferredReady.push_back(std::move(data));
                continue;
            }

            auto it = chunks.find(data->pos);
            if (it == chunks.end()) {
                continue;
            }

            Chunk* ptr = it->second.get();
            pruneRedundantSparseEditsForChunk(data->pos, data->blocks);
            memcpy(ptr->blocks, data->blocks, sizeof(ptr->blocks));
            applySparseEditsToChunk(*ptr);
            ptr->generated = true;
            ptr->generating = false;
            chunkRetryStates.erase(data->pos);
            refreshChunkOccupancy(data->pos);
            markDirty(data->pos);
            markNeighborShellDirty(data->pos);
            integratedCount++;
        }

        if (!deferredReady.empty()) {
            std::lock_guard<std::mutex> lock(asyncChunkState->readyMutex);
            for (auto& data : deferredReady) {
                if (data) {
                    asyncChunkState->readyChunks.push_back(std::move(data));
                }
            }
        }
    }

    // Funcao: retorna 'getChunk' no mundo fractal ativo.
    // Detalhe: usa 'cp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'Chunk*' para dar acesso direto ao objeto resolvido por esta rotina.
    Chunk* getChunk(glm::ivec3 cp) {
        auto it = chunks.find(cp);
        if (it != chunks.end()) {
            return it->second.get();
        }
        queueChunkGeneration(cp);
        return getChunkIfLoaded(cp);
    }

    // Funcao: retorna 'getChunkBlocking' no mundo fractal ativo.
    // Detalhe: usa 'cp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'Chunk*' para dar acesso direto ao objeto resolvido por esta rotina.
    Chunk* getChunkBlocking(glm::ivec3 cp) {
        try {
            auto it = chunks.find(cp);
            if (it == chunks.end()) {
                auto chunk = std::make_unique<Chunk>(cp);
                Chunk* ptr = chunk.get();
                chunks[cp] = std::move(chunk);
                it = chunks.find(cp);
                if (it == chunks.end()) {
                    return ptr;
                }
            }

            Chunk* ptr = it->second.get();
            if (ptr->generated) {
                return ptr;
            }

            generator.generate(*ptr);
            pruneRedundantSparseEditsForChunk(cp, ptr->blocks);
            applySparseEditsToChunk(*ptr);
            ptr->generated = true;
            ptr->generating = false;
            chunkRetryStates.erase(cp);
            refreshChunkOccupancy(cp);
            markDirty(cp);
            markNeighborShellDirty(cp);
            return ptr;
        } catch (const std::bad_alloc&) {
            auto it = chunks.find(cp);
            if (it != chunks.end() && it->second && !it->second->generated) {
                removeChunkOccupancy(cp);
                chunks.erase(it);
            }

            noteMemoryPressure("blocking chunk generation", &cp);
            scheduleChunkRetry(cp, false);
            return nullptr;
        }
    }

    // Funcao: retorna 'getChunkIfLoaded' no mundo fractal ativo.
    // Detalhe: usa 'cp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'Chunk*' para dar acesso direto ao objeto resolvido por esta rotina.
    Chunk* getChunkIfLoaded(glm::ivec3 cp) {
        auto it = chunks.find(cp);
        return it != chunks.end() ? it->second.get() : nullptr;
    }

    // Funcao: retorna 'getChunkIfLoaded' no mundo fractal ativo.
    // Detalhe: usa 'cp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'Chunk*' para dar acesso direto ao objeto resolvido por esta rotina.
    Chunk* getChunkIfLoaded(glm::ivec3 cp) const {
        auto it = chunks.find(cp);
        return it != chunks.end() ? it->second.get() : nullptr;
    }

    // Funcao: retorna 'getBlock' no mundo fractal ativo.
    // Detalhe: usa 'wp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'BlockId' com o resultado composto por esta chamada.
    BlockId getBlock(glm::ivec3 wp) {
        glm::ivec3 cp = worldToChunkPos(wp);
        glm::ivec3 lp = worldToLocalPos(wp);
        Chunk* c = getChunkIfLoaded(cp);
        if (!c || !c->generated) return BlockIds::AIR;
        return c->getBlock(lp.x, lp.y, lp.z);
    }

    // Funcao: retorna 'getBlock' no mundo fractal ativo.
    // Detalhe: usa 'wp' para expor um dado derivado ou um acesso controlado ao estado interno.
    // Retorno: devolve 'BlockId' com o resultado composto por esta chamada.
    BlockId getBlock(glm::ivec3 wp) const {
        glm::ivec3 cp = worldToChunkPos(wp);
        glm::ivec3 lp = worldToLocalPos(wp);
        Chunk* c = getChunkIfLoaded(cp);
        if (!c || !c->generated) return BlockIds::AIR;
        return c->getBlock(lp.x, lp.y, lp.z);
    }

    // Funcao: define 'setBlock' no mundo fractal ativo.
    // Detalhe: usa 'wp', 'type' para aplicar ao componente o valor ou configuracao recebida.
    void setBlock(glm::ivec3 wp, BlockId type) {
        // Toda edicao persistente entra primeiro no mapa esparso e so depois e refletida nos chunks carregados.
        modifications[wp] = type;
        sparseEditIndexDirty = true;
        glm::ivec3 cp = worldToChunkPos(wp);
        glm::ivec3 lp = worldToLocalPos(wp);
        Chunk* c = getChunkIfLoaded(cp);
        if (!c) {
            getChunk(cp);
        } else if (c->generated) {
            c->setBlock(lp.x, lp.y, lp.z, type);
            refreshChunkOccupancy(cp);
        }
        markDirty(cp);
        // Mark neighbor chunks dirty at boundaries
        if (lp.x == 0) markDirty(cp + glm::ivec3(-1,0,0));
        if (lp.x == 15) markDirty(cp + glm::ivec3(1,0,0));
        if (lp.y == 0) markDirty(cp + glm::ivec3(0,-1,0));
        if (lp.y == 15) markDirty(cp + glm::ivec3(0,1,0));
        if (lp.z == 0) markDirty(cp + glm::ivec3(0,0,-1));
        if (lp.z == 15) markDirty(cp + glm::ivec3(0,0,1));
    }

    // Funcao: renderiza 'render' no mundo fractal ativo.
    // Detalhe: usa 'cameraPos', 'viewProjection' para desenhar a saida visual correspondente usando o estado atual.
    void render(const glm::vec3& renderCameraPos, const glm::mat4& renderViewProjection,
                const glm::vec3& cullingCameraPos,
                const glm::mat4& cullingViewProjection) const {
        try {
            (void)renderCameraPos;
            (void)renderViewProjection;
            const glm::ivec3 center =
                worldToChunkPos(glm::ivec3(glm::floor(cullingCameraPos)));
            const int rd = renderDistance;
            const int rd2 = rd * rd;
            const int verticalRadius = verticalChunkRadius();
            const auto renderStart = std::chrono::steady_clock::now();

            ENGINE::Visibility::ClusterVisibilityContext visibilityContext{};
            visibilityContext.cameraPos = cullingCameraPos;
            visibilityContext.viewProjection = cullingViewProjection;
            visibilityContext.frustum =
                ENGINE::Visibility::Frustum::fromViewProjection(cullingViewProjection);
            visibilityContext.clusterChunkDimensions =
                ENGINE::Visibility::kDefaultClusterChunkDimensions;
            visibilityContext.chunkSize = Chunk::SIZE;
            visibilityContext.occluderSolidityThreshold =
                ENGINE::Visibility::kDefaultOccluderSolidityThreshold;
            visibilityContext.enableFrustumCulling = true;
            visibilityContext.enableOcclusionCulling = true;
            visibilityContext.chunkCandidates.reserve(chunks.size());

            for (const auto& [pos, chunk] : chunks) {
                if (!chunk || !chunk->generated || chunk->vertexCount <= 0) {
                    continue;
                }

                glm::ivec3 d = pos - center;
                if (std::abs(d.y) > verticalRadius) {
                    continue;
                }
                const int dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
                if (dist2 > rd2) continue;

                ENGINE::Visibility::ChunkRenderCandidate candidate{};
                candidate.chunkCoord = pos;
                const glm::vec3 minCorner = glm::vec3(pos) * static_cast<float>(Chunk::SIZE);
                const glm::vec3 maxCorner =
                    minCorner + glm::vec3(static_cast<float>(Chunk::SIZE));
                candidate.minCorner = minCorner;
                candidate.maxCorner = maxCorner;
                candidate.vertexCount = chunk->vertexCount;
                candidate.solidVoxelCount = getChunkSolidVoxelCount(pos);
                candidate.userData = chunk.get();
                visibilityContext.chunkCandidates.push_back(candidate);

                const ENGINE::Visibility::ClusterCoord clusterCoord =
                    ENGINE::Visibility::computeClusterCoord(
                        pos, visibilityContext.clusterChunkDimensions);
                if (visibilityContext.clusterSummaries.find(clusterCoord) ==
                    visibilityContext.clusterSummaries.end()) {
                    visibilityContext.clusterSummaries.emplace(
                        clusterCoord, buildClusterSummary(clusterCoord));
                }
            }

            const ENGINE::Visibility::VisibleChunkList visibleChunkList =
                ENGINE::Visibility::buildVisibleChunkList(visibilityContext);

            for (const std::size_t visibleIndex : visibleChunkList.visibleChunkIndices) {
                const ENGINE::Visibility::ChunkRenderCandidate& candidate =
                    visibilityContext.chunkCandidates[visibleIndex];
                const auto* visibleChunk =
                    static_cast<const Chunk*>(candidate.userData);
                if (visibleChunk) {
                    visibleChunk->render();
                }
            }

            lastRenderDiagnostics = RenderDiagnostics{
                visibleChunkList.candidateChunkCount,
                visibleChunkList.frustumCulledChunkCount,
                visibleChunkList.occlusionCulledChunkCount,
                visibleChunkList.visibleClusterCount,
                visibleChunkList.visibleChunkCount,
                visibleChunkList.totalSubmittedVertices,
            };

            const auto renderEnd = std::chrono::steady_clock::now();
            const float totalRenderMs = std::chrono::duration<float, std::milli>(
                                            renderEnd - renderStart)
                                            .count();
            ENGINE::ACCUMULATECHUNKRENDER(totalRenderMs,
                                          visibleChunkList.visibleChunkCount);
        } catch (const std::bad_alloc&) {
            const_cast<FractalWorld*>(this)->noteMemoryPressure("world render");
        }
    }

    void render(const glm::vec3& cameraPos, const glm::mat4& viewProjection) const {
        render(cameraPos, viewProjection, cameraPos, viewProjection);
    }

    // Funcao: executa 'spawnDroppedItem' no mundo fractal ativo.
    // Detalhe: usa 'blockPos', 'type', 'initialVelocity' para encapsular esta etapa especifica do subsistema.
    void spawnDroppedItemAtPosition(const glm::vec3& position,
                                    const InventoryItem& item,
                                    const glm::vec3& initialVelocity = glm::vec3(0.0f),
                                    float pickupDelaySeconds = 0.0f) {
        if (item.empty()) return;

        DroppedItem droppedItem;
        droppedItem.position = position;
        droppedItem.velocity = initialVelocity;
        droppedItem.item = item;
        droppedItem.pickupDelaySeconds = glm::max(0.0f, pickupDelaySeconds);
        const glm::ivec3 spinSeed = glm::ivec3(glm::floor(position));
        droppedItem.spinPhase = static_cast<float>(
            ((spinSeed.x * 73856093) ^ (spinSeed.y * 19349663) ^ (spinSeed.z * 83492791)) & 255) *
            0.024543693f;
        droppedItems.push_back(droppedItem);
    }

    void spawnDroppedItem(glm::ivec3 blockPos, const InventoryItem& item,
                          const glm::vec3& initialVelocity = glm::vec3(0.0f)) {
        spawnDroppedItemAtPosition(glm::vec3(blockPos) + glm::vec3(0.5f), item,
                                   initialVelocity, 0.0f);
    }

    // Enemies are runtime-spawned actors stored alongside the active world state.
    void spawnEnemy(EnemyType type, const glm::vec3& position,
                    float yawDegrees = 0.0f) {
        WorldEnemy enemy;
        std::string error;
        if (!createWorldEnemyInstance(type, position, yawDegrees, enemy, error)) {
            std::printf("[Enemy] %s\n", error.c_str());
            return;
        }

        enemies.push_back(std::move(enemy));
        GameplayStatus::System::instance().recordEnemySpawn(type);
    }

    template <typename TryCollectFn>
    // Funcao: atualiza 'updateDroppedItems' no mundo fractal ativo.
    // Detalhe: usa 'playerPos', 'dt', 'tryCollect' para sincronizar o estado derivado com o frame atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool updateDroppedItems(const glm::vec3& playerPos, float dt, TryCollectFn&& tryCollect) {
        bool pickedUp = false;

        for (size_t i = 0; i < droppedItems.size();) {
            DroppedItem& item = droppedItems[i];
            item.pickupDelaySeconds = glm::max(0.0f, item.pickupDelaySeconds - dt);
            glm::vec3 toPlayer = playerPos - item.position;
            float dist = glm::length(toPlayer);

            if (item.pickupDelaySeconds <= 0.0f && dist <= 2.25f) {
                item.attracting = true;
            }

            if (item.attracting) {
                if (dist > 0.0001f) {
                    const glm::vec3 dir = toPlayer / dist;
                    const float pullStrength =
                        glm::mix(6.0f, 20.0f, glm::clamp(1.0f - dist / 2.25f, 0.0f, 1.0f));
                    item.velocity = glm::mix(item.velocity, dir * pullStrength,
                                             glm::clamp(dt * 8.0f, 0.0f, 1.0f));
                }
            } else {
                simulateDroppedItemPhysics(item, dt);
            }

            item.position += item.velocity * dt;
            resolveDroppedItemCollisions(item);

            toPlayer = playerPos - item.position;
            dist = glm::length(toPlayer);
            if (item.pickupDelaySeconds <= 0.0f && dist <= 0.35f) {
                if (tryCollect(item.item)) {
                    pickedUp = true;
                    droppedItems.erase(droppedItems.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            }

            ++i;
        }

        return pickedUp;
    }

    // Funcao: executa 'worldToChunkPos' no mundo fractal ativo.
    // Detalhe: usa 'wp' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
#pragma endregion

#pragma region FractalWorldCoordinateHelpers
    static glm::ivec3 worldToChunkPos(glm::ivec3 wp) {
        return glm::ivec3(
            wp.x >= 0 ? wp.x / 16 : (wp.x - 15) / 16,
            wp.y >= 0 ? wp.y / 16 : (wp.y - 15) / 16,
            wp.z >= 0 ? wp.z / 16 : (wp.z - 15) / 16
        );
    }

    // Funcao: executa 'worldToLocalPos' no mundo fractal ativo.
    // Detalhe: usa 'wp' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'glm::ivec3' com o resultado composto por esta chamada.
    static glm::ivec3 worldToLocalPos(glm::ivec3 wp) {
        return glm::ivec3(
            ((wp.x % 16) + 16) % 16,
            ((wp.y % 16) + 16) % 16,
            ((wp.z % 16) + 16) % 16
        );
    }

private:
    static constexpr float kDroppedItemGravityAcceleration = 1.71f;

    std::vector<glm::ivec3> chunkLoadOffsets;
    std::deque<glm::ivec3> dirtyChunkQueue;
    std::unordered_map<glm::ivec3, ChunkRetryState, IVec3Hash, IVec3Equal> chunkRetryStates;
    int chunkLoadOffsetsRenderDistance = -1;
    bool sparseEditIndexDirty = true;
    mutable RenderDiagnostics lastRenderDiagnostics{};
    std::unordered_map<glm::ivec3, std::vector<std::pair<glm::ivec3, BlockId>>, IVec3Hash, IVec3Equal>
        chunkModifications;
    std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, IVec3Hash, IVec3Equal> chunkPortals;
    std::unordered_map<glm::ivec3, int, IVec3Hash, IVec3Equal> chunkSolidVoxelCounts;
    std::unordered_map<ENGINE::Visibility::ClusterCoord, int,
                       ENGINE::Visibility::ClusterCoordHash>
        clusterSolidVoxelCounts;
    std::unordered_map<ENGINE::Visibility::ClusterCoord, int,
                       ENGINE::Visibility::ClusterCoordHash>
        clusterLoadedChunkCounts;

    static bool isAllocationFailureMessage(const std::string& message) {
        return message.find("bad allocation") != std::string::npos ||
               message.find("bad alloc") != std::string::npos ||
               message.find("bad_alloc") != std::string::npos ||
               message.find("out of memory") != std::string::npos ||
               message.find("ran out of memory") != std::string::npos;
    }

    bool memoryPressureActive() const {
        return memoryPressureCooldownFrames > 0;
    }

    bool isChunkRetryPending(const glm::ivec3& cp) const {
        return chunkRetryStates.find(cp) != chunkRetryStates.end();
    }

    void scheduleChunkRetry(const glm::ivec3& cp, bool remeshOnRelease) {
        try {
            ChunkRetryState& retryState = chunkRetryStates[cp];
            retryState.cooldownFrames =
                std::max(retryState.cooldownFrames, kChunkRetryCooldownFrames);
            retryState.remeshOnRelease = retryState.remeshOnRelease || remeshOnRelease;
        } catch (const std::bad_alloc&) {
            return;
        }

        if (!remeshOnRelease) {
            return;
        }

        auto it = chunks.find(cp);
        if (it == chunks.end() || !it->second) {
            return;
        }

        it->second->dirty = true;
        it->second->dirtyQueued = false;
    }

    void noteMemoryPressure(const char* origin, const glm::ivec3* chunkPos = nullptr) {
        memoryPressureCooldownFrames =
            std::max(memoryPressureCooldownFrames, kMemoryPressureCooldownFrames);
        if (memoryPressureLogged) {
            return;
        }

        const int queuedChunkJobs = asyncChunkState
                                        ? asyncChunkState->queuedChunkGenerations.load(
                                              std::memory_order_relaxed)
                                        : 0;
        if (chunkPos) {
            std::printf(
                "[World][WARN] Memory pressure detected during %s at (%d, %d, %d) (loaded=%zu, queued=%d, dirty=%zu, renderDistance=%d, budgets=%d/%d/%d).\n",
                origin, chunkPos->x, chunkPos->y, chunkPos->z, chunks.size(),
                queuedChunkJobs, dirtyChunkQueue.size(), renderDistance,
                maxQueuedChunkGenerations, maxReadyChunksPerFrame, maxMeshesPerFrame);
        } else {
            std::printf(
                "[World][WARN] Memory pressure detected during %s (loaded=%zu, queued=%d, dirty=%zu, renderDistance=%d, budgets=%d/%d/%d).\n",
                origin, chunks.size(), queuedChunkJobs, dirtyChunkQueue.size(),
                renderDistance, maxQueuedChunkGenerations, maxReadyChunksPerFrame,
                maxMeshesPerFrame);
        }
        std::fflush(stdout);
        memoryPressureLogged = true;
    }

    void updateMemoryPressureState() {
        if (memoryPressureCooldownFrames > 0) {
            memoryPressureCooldownFrames--;
            if (memoryPressureCooldownFrames == 0) {
                memoryPressureLogged = false;
            }
        }

        for (auto it = chunkRetryStates.begin(); it != chunkRetryStates.end();) {
            if (it->second.cooldownFrames > 0) {
                it->second.cooldownFrames--;
            }

            if (it->second.cooldownFrames > 0) {
                ++it;
                continue;
            }

            const glm::ivec3 retryPos = it->first;
            const bool remeshOnRelease = it->second.remeshOnRelease;
            it = chunkRetryStates.erase(it);

            if (remeshOnRelease) {
                markDirty(retryPos);
            }
        }
    }

    // Funcao: executa 'refreshStreamingBudgets' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region FractalWorldStreamingInternals
    static RuntimePerformanceToggles& runtimePerformanceToggles() {
        static RuntimePerformanceToggles toggles{};
        return toggles;
    }

    static int countSolidVoxels(const Chunk& chunk) {
        int solidVoxelCount = 0;
        for (int x = 0; x < Chunk::SIZE; ++x)
        for (int y = 0; y < Chunk::SIZE; ++y)
        for (int z = 0; z < Chunk::SIZE; ++z) {
            if (isSolid(chunk.blocks[x][y][z])) {
                solidVoxelCount++;
            }
        }
        return solidVoxelCount;
    }

    int getChunkSolidVoxelCount(glm::ivec3 cp) const {
        const auto it = chunkSolidVoxelCounts.find(cp);
        return it != chunkSolidVoxelCounts.end() ? it->second : 0;
    }

    int verticalChunkRadius() const {
        // Infinite-Y streaming is much more expensive than the horizontal shell.
        return std::clamp(2 + renderDistance / 3, 2, 5);
    }

    void refreshChunkOccupancy(glm::ivec3 cp) {
        Chunk* chunk = getChunkIfLoaded(cp);
        if (!chunk || !chunk->generated) {
            removeChunkOccupancy(cp);
            return;
        }

        const ENGINE::Visibility::ClusterCoord clusterCoord =
            ENGINE::Visibility::computeClusterCoord(
                cp, ENGINE::Visibility::kDefaultClusterChunkDimensions);
        const int newSolidVoxelCount = countSolidVoxels(*chunk);

        const auto chunkSolidIt = chunkSolidVoxelCounts.find(cp);
        const bool trackedChunk = chunkSolidIt != chunkSolidVoxelCounts.end();
        const int oldSolidVoxelCount =
            trackedChunk ? chunkSolidIt->second : 0;

        if (!trackedChunk) {
            clusterLoadedChunkCounts[clusterCoord] += 1;
        }

        clusterSolidVoxelCounts[clusterCoord] +=
            newSolidVoxelCount - oldSolidVoxelCount;
        chunkSolidVoxelCounts[cp] = newSolidVoxelCount;

        if (clusterSolidVoxelCounts[clusterCoord] == 0) {
            clusterSolidVoxelCounts.erase(clusterCoord);
        }
    }

    void removeChunkOccupancy(glm::ivec3 cp) {
        const auto chunkSolidIt = chunkSolidVoxelCounts.find(cp);
        if (chunkSolidIt == chunkSolidVoxelCounts.end()) {
            return;
        }

        const ENGINE::Visibility::ClusterCoord clusterCoord =
            ENGINE::Visibility::computeClusterCoord(
                cp, ENGINE::Visibility::kDefaultClusterChunkDimensions);
        const int solidVoxelCount = chunkSolidIt->second;
        chunkSolidVoxelCounts.erase(chunkSolidIt);

        auto clusterSolidIt = clusterSolidVoxelCounts.find(clusterCoord);
        if (clusterSolidIt != clusterSolidVoxelCounts.end()) {
            clusterSolidIt->second -= solidVoxelCount;
            if (clusterSolidIt->second <= 0) {
                clusterSolidVoxelCounts.erase(clusterSolidIt);
            }
        }

        auto clusterLoadedIt = clusterLoadedChunkCounts.find(clusterCoord);
        if (clusterLoadedIt != clusterLoadedChunkCounts.end()) {
            clusterLoadedIt->second -= 1;
            if (clusterLoadedIt->second <= 0) {
                clusterLoadedChunkCounts.erase(clusterLoadedIt);
            }
        }
    }

    ENGINE::Visibility::ClusterSummary buildClusterSummary(
        const ENGINE::Visibility::ClusterCoord& clusterCoord) const {
        ENGINE::Visibility::ClusterSummary summary{};
        summary.coord = clusterCoord;
        summary.bounds = ENGINE::Visibility::computeClusterBounds(
            clusterCoord, Chunk::SIZE,
            ENGINE::Visibility::kDefaultClusterChunkDimensions);

        const auto solidIt = clusterSolidVoxelCounts.find(clusterCoord);
        if (solidIt != clusterSolidVoxelCounts.end()) {
            summary.solidVoxelCount = solidIt->second;
        }

        const auto loadedIt = clusterLoadedChunkCounts.find(clusterCoord);
        if (loadedIt != clusterLoadedChunkCounts.end()) {
            summary.loadedChunkCount = loadedIt->second;
        }

        const glm::ivec3 clusterDims =
            ENGINE::Visibility::kDefaultClusterChunkDimensions;
        const int clusterCapacity =
            clusterDims.x * clusterDims.y * clusterDims.z *
            Chunk::SIZE * Chunk::SIZE * Chunk::SIZE;
        if (clusterCapacity > 0) {
            summary.solidVoxelRatio =
                static_cast<float>(summary.solidVoxelCount) /
                static_cast<float>(clusterCapacity);
        }

        return summary;
    }

    void refreshStreamingBudgets() {
        const int workerCount =
            std::max(1, static_cast<int>(chunkThreadPool.threadCount()));
        const int distanceScale = std::clamp(renderDistance, 2, 10);
        const int verticalScale = verticalChunkRadius();
        const int queuedBudget =
            std::clamp(3 + workerCount + distanceScale / 3 + verticalScale, 4, 12);
        const int readyBudget = std::clamp(1 + workerCount / 2, 2, 4);
        const int meshBudget = std::clamp(1 + workerCount / 3, 1, 2);

        if (memoryPressureActive()) {
            maxQueuedChunkGenerations = std::max(2, queuedBudget / 2);
            maxReadyChunksPerFrame = std::max(1, readyBudget - 1);
            maxMeshesPerFrame = 1;
            return;
        }

        maxQueuedChunkGenerations = queuedBudget;
        maxReadyChunksPerFrame = readyBudget;
        maxMeshesPerFrame = meshBudget;
    }

    // Funcao: normaliza 'sanitizeViewForward' no mundo fractal ativo.
    // Detalhe: usa 'viewForward' para limpar a entrada para reduzir inconsistencias antes do uso.
    // Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
    glm::vec3 sanitizeViewForward(const glm::vec3& viewForward) const {
        if (glm::dot(viewForward, viewForward) <= 1e-5f) {
            return glm::vec3(0.0f, 0.0f, -1.0f);
        }
        return glm::normalize(viewForward);
    }

    // Funcao: executa 'chunkDistanceSquared' no mundo fractal ativo.
    // Detalhe: usa 'offset' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'int' com o valor numerico calculado para a proxima decisao do pipeline.
    int chunkDistanceSquared(const glm::ivec3& offset) const {
        return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    }

    // Funcao: executa 'chunkViewAlignment' no mundo fractal ativo.
    // Detalhe: usa 'offset', 'viewForward' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
    float chunkViewAlignment(const glm::ivec3& offset,
                             const glm::vec3& viewForward) const {
        if (offset == glm::ivec3(0)) {
            return 1.0f;
        }
        return glm::dot(glm::normalize(glm::vec3(offset)),
                        sanitizeViewForward(viewForward));
    }

    // Funcao: executa 'chunkPriorityScore' no mundo fractal ativo.
    // Detalhe: usa 'offset', 'viewForward' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'float' com o valor numerico calculado para a proxima decisao do pipeline.
    float chunkPriorityScore(const glm::ivec3& offset,
                             const glm::vec3& viewForward) const {
        const float distance2 = static_cast<float>(chunkDistanceSquared(offset));
        if (distance2 <= 0.001f) {
            return -1000.0f;
        }

        const float alignment = chunkViewAlignment(offset, viewForward);
        const float frontBonus =
            alignment > 0.0f ? alignment * (6.0f + distance2 * 0.45f)
                             : alignment * 1.5f;
        const float verticalPenalty =
            static_cast<float>(std::abs(offset.y)) * 0.35f;
        return distance2 + verticalPenalty - frontBonus;
    }

    // Funcao: executa 'prefersChunkOffset' no mundo fractal ativo.
    // Detalhe: usa 'a', 'b', 'viewForward' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool prefersChunkOffset(const glm::ivec3& a, const glm::ivec3& b,
                            const glm::vec3& viewForward) const {
        const float scoreA = chunkPriorityScore(a, viewForward);
        const float scoreB = chunkPriorityScore(b, viewForward);
        if (scoreA != scoreB) {
            return scoreA < scoreB;
        }

        const int distA = chunkDistanceSquared(a);
        const int distB = chunkDistanceSquared(b);
        if (distA != distB) {
            return distA < distB;
        }

        const int aya = std::abs(a.y);
        const int ayb = std::abs(b.y);
        if (aya != ayb) {
            return aya < ayb;
        }

        const int axa = std::abs(a.x);
        const int axb = std::abs(b.x);
        if (axa != axb) {
            return axa < axb;
        }

        const int aza = std::abs(a.z);
        const int azb = std::abs(b.z);
        if (aza != azb) {
            return aza < azb;
        }

        if (a.y != b.y) {
            return a.y < b.y;
        }
        if (a.x != b.x) {
            return a.x < b.x;
        }
        return a.z < b.z;
    }

    // Funcao: verifica 'isFrontFacingOffset' no mundo fractal ativo.
    // Detalhe: usa 'offset', 'viewForward' para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool isFrontFacingOffset(const glm::ivec3& offset,
                             const glm::vec3& viewForward) const {
        if (offset == glm::ivec3(0)) {
            return true;
        }
        return glm::dot(glm::vec3(offset), sanitizeViewForward(viewForward)) >= 0.0f;
    }

    // Funcao: executa 'queueChunkGeneration' no mundo fractal ativo.
    // Detalhe: usa 'cp' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool queueChunkGeneration(glm::ivec3 cp) {
        // A geracao roda em copias isoladas do gerador e devolve apenas dados crus de blocos.
        if (!asyncChunkState) {
            return false;
        }

        if (isChunkRetryPending(cp)) {
            return false;
        }

        if (chunks.find(cp) != chunks.end()) {
            return false;
        }

        bool generationQueued = false;
        try {
            auto chunk = std::make_unique<Chunk>(cp);
            chunk->generating = true;
            chunks[cp] = std::move(chunk);

            WorldGenerator genCopy = generator;
            std::shared_ptr<AsyncChunkState> state = asyncChunkState;
            asyncChunkState->queuedChunkGenerations.fetch_add(1, std::memory_order_relaxed);
            generationQueued = true;

            chunkThreadPool.enqueue([cp, genCopy, state]() mutable {
                if (!state) {
                    return;
                }

                struct TaskGuard {
                    std::shared_ptr<AsyncChunkState> state;
                    ~TaskGuard() {
                        if (state) {
                            state->queuedChunkGenerations.fetch_sub(
                                1, std::memory_order_relaxed);
                        }
                    }
                };
                TaskGuard guard{state};

                if (!state->acceptingResults.load(std::memory_order_acquire)) {
                    return;
                }

                try {
                    auto data = std::make_unique<ChunkData>();
                    data->pos = cp;

                    Chunk temp(cp);
                    genCopy.generate(temp);
                    memcpy(data->blocks, temp.blocks, sizeof(temp.blocks));

                    if (!state->acceptingResults.load(std::memory_order_acquire)) {
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(state->readyMutex);
                        if (state->acceptingResults.load(std::memory_order_relaxed)) {
                            state->readyChunks.push_back(std::move(data));
                        }
                    }
                } catch (const std::exception& exception) {
                    try {
                        std::lock_guard<std::mutex> lock(state->readyMutex);
                        if (state->acceptingResults.load(std::memory_order_relaxed)) {
                            state->failedChunks.push_back(
                                {cp, std::string(exception.what())});
                        }
                    } catch (...) {
                        std::fprintf(stderr,
                                     "[World][WARN] Chunk generation failed at (%d, %d, %d), and the failure could not be queued safely.\n",
                                     cp.x, cp.y, cp.z);
                        std::fflush(stderr);
                    }
                } catch (...) {
                    try {
                        std::lock_guard<std::mutex> lock(state->readyMutex);
                        if (state->acceptingResults.load(std::memory_order_relaxed)) {
                            state->failedChunks.push_back(
                                {cp, "unknown exception"});
                        }
                    } catch (...) {
                        std::fprintf(stderr,
                                     "[World][WARN] Chunk generation failed at (%d, %d, %d) with an unknown exception, and the failure could not be queued safely.\n",
                                     cp.x, cp.y, cp.z);
                        std::fflush(stderr);
                    }
                }
            });
            return true;
        } catch (const std::bad_alloc&) {
            if (generationQueued && asyncChunkState) {
                asyncChunkState->queuedChunkGenerations.fetch_sub(
                    1, std::memory_order_relaxed);
            }

            auto it = chunks.find(cp);
            if (it != chunks.end()) {
                removeChunkOccupancy(cp);
                chunks.erase(it);
            }

            noteMemoryPressure("chunk generation queue", &cp);
            scheduleChunkRetry(cp, false);
            return false;
        }
    }

    // Funcao: simula 'simulateDroppedItemPhysics' no mundo fractal ativo.
    // Detalhe: usa 'item', 'dt' para avancar a regra de jogo ou fisica usando o intervalo informado.
#pragma endregion

    #pragma region FractalWorldDroppedItems
    void simulateDroppedItemPhysics(DroppedItem& item, float dt) {
        // Martian gravity with gentle drag keeps motion readable and stable.
        item.velocity.y -= kDroppedItemGravityAcceleration * dt;
        const float damping = std::pow(0.985f, dt * 60.0f);
        item.velocity *= damping;
    }

    // Funcao: resolve 'resolveDroppedItemCollisions' no mundo fractal ativo.
    // Detalhe: usa 'item' para traduzir o estado atual para uma resposta concreta usada pelo restante do sistema.
    void resolveDroppedItemCollisions(DroppedItem& item) {
        const float r = 0.12f;
        const glm::ivec3 minB = glm::ivec3(glm::floor(item.position - glm::vec3(r)));
        const glm::ivec3 maxB = glm::ivec3(glm::floor(item.position + glm::vec3(r)));

        for (int bx = minB.x; bx <= maxB.x; bx++)
        for (int by = minB.y; by <= maxB.y; by++)
        for (int bz = minB.z; bz <= maxB.z; bz++) {
            const glm::ivec3 block(bx, by, bz);
            if (!isSolid(getBlock(block))) {
                continue;
            }

            const float closestX = glm::clamp(item.position.x, static_cast<float>(bx),
                                              static_cast<float>(bx) + 1.0f);
            const float closestY = glm::clamp(item.position.y, static_cast<float>(by),
                                              static_cast<float>(by) + 1.0f);
            const float closestZ = glm::clamp(item.position.z, static_cast<float>(bz),
                                              static_cast<float>(bz) + 1.0f);

            const glm::vec3 closest(closestX, closestY, closestZ);
            const glm::vec3 diff = item.position - closest;
            const float dist = glm::length(diff);
            if (dist >= r || dist <= 0.0001f) {
                continue;
            }

            const glm::vec3 pushDir = glm::normalize(diff);
            item.position += pushDir * (r - dist);

            const float vDot = glm::dot(item.velocity, pushDir);
            if (vDot < 0.0f) {
                item.velocity -= pushDir * vDot * 1.8f;
            }
        }
    }

    // Funcao: executa 'rebuildChunkLoadOffsets' no mundo fractal ativo.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
#pragma endregion

#pragma region FractalWorldChunkMaintenance
    void rebuildChunkLoadOffsets() {
        chunkLoadOffsetsRenderDistance = renderDistance;
        chunkLoadOffsets.clear();

        const int rd = renderDistance;
        const int verticalRadius = verticalChunkRadius();
        const int side = (rd * 2) + 1;
        chunkLoadOffsets.reserve(static_cast<size_t>(side) *
                                 static_cast<size_t>((verticalRadius * 2) + 1) *
                                 static_cast<size_t>(side));

        for (int x = -rd; x <= rd; x++)
        for (int y = -verticalRadius; y <= verticalRadius; y++)
        for (int z = -rd; z <= rd; z++) {
            chunkLoadOffsets.push_back(glm::ivec3(x, y, z));
        }

        std::sort(chunkLoadOffsets.begin(), chunkLoadOffsets.end(),
                  [](const glm::ivec3& a, const glm::ivec3& b) {
                      const int da = a.x * a.x + a.y * a.y + a.z * a.z;
                      const int db = b.x * b.x + b.y * b.y + b.z * b.z;
                      if (da != db) return da < db;

                      const int aya = std::abs(a.y), ayb = std::abs(b.y);
                      if (aya != ayb) return aya < ayb;
                      const int axa = std::abs(a.x), axb = std::abs(b.x);
                      if (axa != axb) return axa < axb;
                      const int aza = std::abs(a.z), azb = std::abs(b.z);
                      if (aza != azb) return aza < azb;

                      if (a.y != b.y) return a.y < b.y;
                      if (a.x != b.x) return a.x < b.x;
                      return a.z < b.z;
                  });
    }

    // Funcao: executa 'markDirty' no mundo fractal ativo.
    // Detalhe: usa 'cp' para encapsular esta etapa especifica do subsistema.
    void markDirty(glm::ivec3 cp) {
        auto it = chunks.find(cp);
        if (it == chunks.end()) return;

        Chunk* chunk = it->second.get();
        chunk->dirty = true;
        if (isChunkRetryPending(cp)) return;
        if (chunk->dirtyQueued) return;

        try {
            dirtyChunkQueue.push_back(cp);
            chunk->dirtyQueued = true;
        } catch (const std::bad_alloc&) {
            chunk->dirtyQueued = false;
            noteMemoryPressure("dirty queue enqueue", &cp);
            scheduleChunkRetry(cp, true);
        }
    }

    // Funcao: executa 'markNeighborShellDirty' no mundo fractal ativo.
    // Detalhe: usa 'cp' para encapsular esta etapa especifica do subsistema.
    void markNeighborShellDirty(glm::ivec3 cp) {
        static const glm::ivec3 offsets[6] = {
            {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
            {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
        };
        for (const glm::ivec3& offset : offsets) {
            markDirty(cp + offset);
        }
    }

    // Funcao: aplica 'applySparseEditsToChunk' no mundo fractal ativo.
    // Detalhe: usa 'chunk' para propagar o efeito calculado sobre o estado do jogo ou do subsistema.
    void applySparseEditsToChunk(Chunk& chunk) {
        // O indice esparso agrupa edits por chunk para evitar procurar todas as modificacoes a cada integracao.
        if (sparseEditIndexDirty) {
            rebuildSparseEditIndex();
        }

        const glm::ivec3 cp = chunk.chunkPos;
        auto chunkEditIt = chunkModifications.find(cp);
        if (chunkEditIt != chunkModifications.end()) {
            for (const auto& [wp, bt] : chunkEditIt->second) {
                glm::ivec3 lp = worldToLocalPos(wp);
                chunk.blocks[lp.x][lp.y][lp.z] = bt;
            }
        }

        auto chunkPortalIt = chunkPortals.find(cp);
        if (chunkPortalIt != chunkPortals.end()) {
            for (const glm::ivec3& wp : chunkPortalIt->second) {
                glm::ivec3 lp = worldToLocalPos(wp);
                chunk.blocks[lp.x][lp.y][lp.z] = BlockIds::PORTAL;
            }
        }
    }

    // Funcao: carrega 'loadChunksAround' no mundo fractal ativo.
    // Detalhe: usa 'center', 'viewForward' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
    void loadChunksAround(glm::ivec3 center, const glm::vec3& viewForward) {
        // A fila e preenchida em duas passadas: primeiro offsets a frente da camera, depois o resto.
        if (chunkLoadOffsetsRenderDistance != renderDistance || chunkLoadOffsets.empty()) {
            rebuildChunkLoadOffsets();
        }

        const auto hasQueueBudget = [this]() {
            return asyncChunkState &&
                   asyncChunkState->queuedChunkGenerations.load(std::memory_order_relaxed) <
                       maxQueuedChunkGenerations;
        };

        for (const glm::ivec3& offset : chunkLoadOffsets) {
            if (!hasQueueBudget()) {
                break;
            }
            if (!isFrontFacingOffset(offset, viewForward)) {
                continue;
            }

            const glm::ivec3 cp = center + offset;
            if (chunks.find(cp) == chunks.end()) {
                queueChunkGeneration(cp);
            }
        }

        for (const glm::ivec3& offset : chunkLoadOffsets) {
            if (!hasQueueBudget()) {
                break;
            }
            if (isFrontFacingOffset(offset, viewForward)) {
                continue;
            }

            const glm::ivec3 cp = center + offset;
            if (chunks.find(cp) == chunks.end()) {
                queueChunkGeneration(cp);
            }
        }
    }

    // Funcao: executa 'unloadDistantChunks' no mundo fractal ativo.
    // Detalhe: usa 'center' para encapsular esta etapa especifica do subsistema.
    void unloadDistantChunks(glm::ivec3 center) {
        const int maxDistXZ = renderDistance + 3;
        const int maxDistY = verticalChunkRadius() + 2;
        std::vector<glm::ivec3> toRemove;
        for (auto& [pos, chunk] : chunks) {
            glm::ivec3 d = pos - center;
            if (std::abs(d.x) > maxDistXZ || std::abs(d.y) > maxDistY ||
                std::abs(d.z) > maxDistXZ) {
                if (!chunk->generating) {
                    toRemove.push_back(pos);
                }
            }
        }
        for (auto& p : toRemove) {
            removeChunkOccupancy(p);
            chunks.erase(p);
            chunkRetryStates.erase(p);
        }
    }

    // Funcao: executa 'rebuildDirtyMeshes' no mundo fractal ativo.
    // Detalhe: usa 'center', 'viewForward' para encapsular esta etapa especifica do subsistema.
    void rebuildDirtyMeshes(glm::ivec3 center, const glm::vec3& viewForward) {
        // Remesh e caro; por isso chunks sujos entram em fila e sao processados por prioridade limitada por frame.
        struct DirtyCandidate {
            glm::ivec3 pos;
            float priorityScore = 0.0f;
        };
        if (dirtyChunkQueue.empty()) return;

        const int dequeueBudget = std::max(maxMeshesPerFrame * 8, maxMeshesPerFrame);
        std::vector<DirtyCandidate> candidates;
        candidates.reserve(dequeueBudget);

        int dequeued = 0;
        while (dequeued < dequeueBudget && !dirtyChunkQueue.empty()) {
            const glm::ivec3 pos = dirtyChunkQueue.front();
            dirtyChunkQueue.pop_front();
            dequeued++;

            auto it = chunks.find(pos);
            if (it == chunks.end()) continue;

            Chunk* chunk = it->second.get();
            chunk->dirtyQueued = false;
            if (!chunk->dirty || !chunk->generated) continue;

            candidates.push_back(
                DirtyCandidate{pos, chunkPriorityScore(pos - center, viewForward)});
        }
        if (candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(),
                  [](const DirtyCandidate& a, const DirtyCandidate& b) {
                      return a.priorityScore < b.priorityScore;
                  });

        int meshedThisFrame = 0;
        for (std::size_t candidateIndex = 0; candidateIndex < candidates.size();
             candidateIndex++) {
            const DirtyCandidate& candidate = candidates[candidateIndex];
            auto it = chunks.find(candidate.pos);
            if (it == chunks.end()) continue;
            Chunk* chunk = it->second.get();
            if (!chunk->dirty || !chunk->generated) continue;

            if (meshedThisFrame >= maxMeshesPerFrame) {
                markDirty(candidate.pos);
                continue;
            }

            auto readyNeighbor = [this](glm::ivec3 pos) -> Chunk* {
                Chunk* neighbor = getChunkIfLoaded(pos);
                return (neighbor && neighbor->generated) ? neighbor : nullptr;
            };

            Chunk* nx = readyNeighbor(candidate.pos + glm::ivec3(-1,0,0));
            Chunk* px = readyNeighbor(candidate.pos + glm::ivec3(1,0,0));
            Chunk* ny = readyNeighbor(candidate.pos + glm::ivec3(0,-1,0));
            Chunk* py = readyNeighbor(candidate.pos + glm::ivec3(0,1,0));
            Chunk* nz = readyNeighbor(candidate.pos + glm::ivec3(0,0,-1));
            Chunk* pz = readyNeighbor(candidate.pos + glm::ivec3(0,0,1));

            try {
                chunk->buildMesh(depth, nx, px, ny, py, nz, pz,
                                 isGreedyMeshingEnabled());
                chunkRetryStates.erase(candidate.pos);
                meshedThisFrame++;
            } catch (const std::bad_alloc&) {
                noteMemoryPressure("chunk mesh rebuild", &candidate.pos);
                scheduleChunkRetry(candidate.pos, true);
                for (std::size_t retryIndex = candidateIndex + 1u;
                     retryIndex < candidates.size(); retryIndex++) {
                    markDirty(candidates[retryIndex].pos);
                }
                break;
            }
        }
    }
};
#pragma endregion
