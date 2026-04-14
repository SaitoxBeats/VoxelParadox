#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "engine/thread_pool.hpp"
#include "world/biome/biome_preset.hpp"
#include "world/generation/chunk.hpp"
#include "world/generation/chunk_generator_source.hpp"
#include "world/generation/fractal_world.hpp"

namespace BiomeMaker {

using namespace VoxelGame;

class PreviewWorldController {
public:
  struct Stats {
    std::uint64_t generationRevision = 0;
    int loadedChunks = 0;
    int generatedChunks = 0;
    int queuedGenerations = 0;
    int lastReadyChunkResults = 0;
    int streamRenderDistance = 5;
    PreviewMode mode = PreviewMode::SANDBOX;
  };

  PreviewWorldController();
  ~PreviewWorldController();

  void clear();

  void setGeneratorSource(std::shared_ptr<const IChunkGeneratorSource> source);
  void setMode(PreviewMode mode);
  void setSandboxRadius(const glm::ivec3& radius);
  void setStreamRenderDistance(int renderDistance);
  void setAnchorChunk(const glm::ivec3& anchorChunk);

  void update(const glm::vec3& cameraPosition);
  void render(const glm::vec3& cameraPosition, const glm::mat4& viewProjection);

  int depth() const;
  std::uint32_t seed() const;
  const BiomePreset* biomePreset() const;
  int previewRenderDistance() const;
  const glm::ivec3& anchorChunk() const { return anchorChunk_; }
  std::uint64_t generationRevision() const { return generationRevision_; }
  const Stats& stats() const { return stats_; }

private:
  struct PreviewChunkEntry {
    std::unique_ptr<Chunk> chunk;
    std::uint64_t appliedRevision = 0;
    std::uint64_t lastQueuedRevision = 0;
  };

  struct ReadyChunkResult {
    glm::ivec3 pos{0};
    std::uint64_t revision = 0;
    BlockId blocks[Chunk::SIZE][Chunk::SIZE][Chunk::SIZE]{};
  };

  struct AsyncState {
    std::mutex readyMutex;
    std::vector<std::unique_ptr<ReadyChunkResult>> readyResults;
    std::atomic<bool> acceptingResults{true};
    std::atomic<int> queuedChunkGenerations{0};
  };

  struct ViewFrustum {
    std::array<glm::vec4, 6> planes{};

    static ViewFrustum fromViewProjection(const glm::mat4& viewProjection);
    bool intersectsAabb(const glm::vec3& minCorner,
                        const glm::vec3& maxCorner) const;
  };

  inline static ThreadPool previewThreadPool{4};

  std::shared_ptr<AsyncState> asyncState_;
  std::shared_ptr<const IChunkGeneratorSource> source_;
  std::unordered_map<glm::ivec3, PreviewChunkEntry, IVec3Hash> chunks_;
  std::deque<glm::ivec3> dirtyChunkQueue_;
  std::vector<glm::ivec3> streamLoadOffsets_;

  int streamOffsetsRenderDistance_ = -1;
  int maxQueuedChunkGenerations_ = 16;
  int maxReadyChunksPerFrame_ = 8;
  int maxMeshesPerFrame_ = 4;
  std::uint64_t generationRevision_ = 1;
  PreviewMode mode_ = PreviewMode::SANDBOX;
  glm::ivec3 sandboxRadius_{2, 1, 2};
  int streamRenderDistance_ = 5;
  glm::ivec3 anchorChunk_{0};
  Stats stats_{};

  void shutdownAsyncState();
  void processReadyChunks();
  void collectTargetChunks(const glm::vec3& cameraPosition,
                           std::vector<glm::ivec3>& outChunks) const;
  void requestChunkGeneration(const glm::ivec3& chunkPos, PreviewChunkEntry& entry);
  void unloadOutsideTarget(const std::unordered_set<glm::ivec3, IVec3Hash>& targetChunks);
  void rebuildDirtyMeshes(const glm::ivec3& centerChunk);
  void rebuildStreamLoadOffsets();
  void markDirty(const glm::ivec3& chunkPos);
  void markNeighborShellDirty(const glm::ivec3& chunkPos);
};

} // namespace BiomeMaker
