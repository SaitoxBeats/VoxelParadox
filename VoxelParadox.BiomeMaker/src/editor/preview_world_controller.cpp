#include "preview_world_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "engine/engine.hpp"

namespace BiomeMaker {

PreviewWorldController::PreviewWorldController() {
  asyncState_ = std::make_shared<AsyncState>();
  stats_.generationRevision = generationRevision_;
  stats_.streamRenderDistance = streamRenderDistance_;
  stats_.mode = mode_;
}

PreviewWorldController::~PreviewWorldController() {
  clear();
  shutdownAsyncState();
}

void PreviewWorldController::clear() {
  shutdownAsyncState();
  chunks_.clear();
  dirtyChunkQueue_.clear();
  asyncState_ = std::make_shared<AsyncState>();
  stats_ = {};
  stats_.generationRevision = generationRevision_;
  stats_.streamRenderDistance = streamRenderDistance_;
  stats_.mode = mode_;
}

void PreviewWorldController::setGeneratorSource(
    std::shared_ptr<const IChunkGeneratorSource> source) {
  source_ = std::move(source);
  generationRevision_++;
  stats_.generationRevision = generationRevision_;
}

void PreviewWorldController::setMode(PreviewMode mode) {
  mode_ = mode;
  stats_.mode = mode_;
}

void PreviewWorldController::setSandboxRadius(const glm::ivec3& radius) {
  sandboxRadius_ = glm::max(radius, glm::ivec3(0));
}

void PreviewWorldController::setStreamRenderDistance(int renderDistance) {
  streamRenderDistance_ = std::clamp(renderDistance, 2, 10);
  streamOffsetsRenderDistance_ = -1;
  stats_.streamRenderDistance = streamRenderDistance_;
}

void PreviewWorldController::setAnchorChunk(const glm::ivec3& anchorChunk) {
  anchorChunk_ = anchorChunk;
}

void PreviewWorldController::update(const glm::vec3& cameraPosition) {
  processReadyChunks();

  if (!source_) {
    stats_.loadedChunks = 0;
    stats_.generatedChunks = 0;
    stats_.queuedGenerations = 0;
    return;
  }

  if (mode_ == PreviewMode::STREAMING &&
      (streamOffsetsRenderDistance_ != streamRenderDistance_ ||
       streamLoadOffsets_.empty())) {
    rebuildStreamLoadOffsets();
  }

  std::vector<glm::ivec3> targetChunks;
  collectTargetChunks(cameraPosition, targetChunks);
  std::unordered_set<glm::ivec3, IVec3Hash> targetSet(targetChunks.begin(),
                                                      targetChunks.end());

  for (const glm::ivec3& chunkPos : targetChunks) {
    PreviewChunkEntry& entry = chunks_[chunkPos];
    if (!entry.chunk) {
      entry.chunk = std::make_unique<Chunk>(chunkPos);
    }

    if (entry.appliedRevision < generationRevision_ &&
        entry.lastQueuedRevision < generationRevision_ &&
        asyncState_->queuedChunkGenerations.load(std::memory_order_relaxed) <
            maxQueuedChunkGenerations_) {
      requestChunkGeneration(chunkPos, entry);
    }
  }

  unloadOutsideTarget(targetSet);

  const glm::ivec3 centerChunk =
      FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(cameraPosition)));
  rebuildDirtyMeshes(centerChunk);

  int generatedChunkCount = 0;
  for (const auto& [chunkPos, entry] : chunks_) {
    (void)chunkPos;
    if (entry.chunk && entry.chunk->generated.load(std::memory_order_relaxed)) {
      generatedChunkCount++;
    }
  }

  stats_.loadedChunks = static_cast<int>(chunks_.size());
  stats_.generatedChunks = generatedChunkCount;
  stats_.queuedGenerations =
      asyncState_->queuedChunkGenerations.load(std::memory_order_relaxed);
  stats_.generationRevision = generationRevision_;
  stats_.streamRenderDistance = streamRenderDistance_;
  stats_.mode = mode_;
}

void PreviewWorldController::render(const glm::vec3& cameraPosition,
                                    const glm::mat4& viewProjection) {
  const auto renderStart = std::chrono::steady_clock::now();
  const ViewFrustum frustum = ViewFrustum::fromViewProjection(viewProjection);
  const glm::ivec3 centerChunk =
      FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(cameraPosition)));
  const int renderDistance = previewRenderDistance();
  const int renderDistance2 = renderDistance * renderDistance;
  int renderedChunkCount = 0;

  for (auto& [chunkPos, entry] : chunks_) {
    if (!entry.chunk || !entry.chunk->generated.load(std::memory_order_relaxed)) {
      continue;
    }

    if (mode_ == PreviewMode::STREAMING) {
      const glm::ivec3 delta = chunkPos - centerChunk;
      const int distance2 =
          delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
      if (distance2 > renderDistance2) {
        continue;
      }
    }

    const glm::vec3 minCorner = glm::vec3(chunkPos) * static_cast<float>(Chunk::SIZE);
    const glm::vec3 maxCorner =
        minCorner + glm::vec3(static_cast<float>(Chunk::SIZE));
    if (!frustum.intersectsAabb(minCorner, maxCorner)) {
      continue;
    }

    entry.chunk->render();
    renderedChunkCount++;
  }

  const auto renderEnd = std::chrono::steady_clock::now();
  const float totalRenderMs =
      std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
  ENGINE::ACCUMULATECHUNKRENDER(totalRenderMs, renderedChunkCount);
}

int PreviewWorldController::depth() const {
  return source_ ? source_->depth() : 0;
}

std::uint32_t PreviewWorldController::seed() const {
  return source_ ? source_->seed() : 0u;
}

const BiomePreset* PreviewWorldController::biomePreset() const {
  return source_ ? source_->biomePreset() : nullptr;
}

int PreviewWorldController::previewRenderDistance() const {
  if (mode_ == PreviewMode::STREAMING) {
    return streamRenderDistance_;
  }
  return std::max({sandboxRadius_.x, sandboxRadius_.y, sandboxRadius_.z}) + 2;
}

void PreviewWorldController::shutdownAsyncState() {
  if (!asyncState_) {
    return;
  }

  asyncState_->acceptingResults.store(false, std::memory_order_release);
  while (asyncState_->queuedChunkGenerations.load(std::memory_order_acquire) > 0) {
    std::this_thread::yield();
  }

  std::lock_guard<std::mutex> lock(asyncState_->readyMutex);
  asyncState_->readyResults.clear();
}

void PreviewWorldController::processReadyChunks() {
  if (!asyncState_) {
    return;
  }

  std::vector<std::unique_ptr<ReadyChunkResult>> localReady;
  {
    std::lock_guard<std::mutex> lock(asyncState_->readyMutex);
    const std::size_t count = std::min(
        asyncState_->readyResults.size(),
        static_cast<std::size_t>(maxReadyChunksPerFrame_));
    localReady.reserve(count);
    for (std::size_t index = 0; index < count; index++) {
      localReady.push_back(std::move(asyncState_->readyResults.back()));
      asyncState_->readyResults.pop_back();
    }
  }

  stats_.lastReadyChunkResults = static_cast<int>(localReady.size());

  for (auto& readyChunk : localReady) {
    auto chunkIt = chunks_.find(readyChunk->pos);
    if (chunkIt == chunks_.end() || !chunkIt->second.chunk) {
      continue;
    }

    PreviewChunkEntry& entry = chunkIt->second;
    Chunk& chunk = *entry.chunk;
    if (readyChunk->revision != generationRevision_ ||
        readyChunk->revision != entry.lastQueuedRevision) {
      chunk.generating.store(entry.appliedRevision < entry.lastQueuedRevision,
                             std::memory_order_relaxed);
      continue;
    }

    std::memcpy(chunk.blocks, readyChunk->blocks, sizeof(chunk.blocks));
    chunk.generated.store(true, std::memory_order_relaxed);
    entry.appliedRevision = readyChunk->revision;
    chunk.generating.store(false, std::memory_order_relaxed);
    markDirty(readyChunk->pos);
    markNeighborShellDirty(readyChunk->pos);
  }
}

void PreviewWorldController::collectTargetChunks(
    const glm::vec3& cameraPosition, std::vector<glm::ivec3>& outChunks) const {
  outChunks.clear();

  if (mode_ == PreviewMode::SANDBOX) {
    const int width = sandboxRadius_.x * 2 + 1;
    const int height = sandboxRadius_.y * 2 + 1;
    const int depth = sandboxRadius_.z * 2 + 1;
    outChunks.reserve(static_cast<std::size_t>(width * height * depth));
    for (int x = -sandboxRadius_.x; x <= sandboxRadius_.x; x++) {
      for (int y = -sandboxRadius_.y; y <= sandboxRadius_.y; y++) {
        for (int z = -sandboxRadius_.z; z <= sandboxRadius_.z; z++) {
          outChunks.push_back(anchorChunk_ + glm::ivec3(x, y, z));
        }
      }
    }
    return;
  }

  const glm::ivec3 centerChunk =
      FractalWorld::worldToChunkPos(glm::ivec3(glm::floor(cameraPosition)));
  outChunks.reserve(streamLoadOffsets_.size());
  for (const glm::ivec3& offset : streamLoadOffsets_) {
    outChunks.push_back(centerChunk + offset);
  }
}

void PreviewWorldController::requestChunkGeneration(const glm::ivec3& chunkPos,
                                                    PreviewChunkEntry& entry) {
  if (!asyncState_ || !source_) {
    return;
  }

  entry.lastQueuedRevision = generationRevision_;
  entry.chunk->generating.store(true, std::memory_order_relaxed);

  std::shared_ptr<const IChunkGeneratorSource> sourceSnapshot = source_;
  std::shared_ptr<AsyncState> state = asyncState_;
  const std::uint64_t revision = generationRevision_;
  state->queuedChunkGenerations.fetch_add(1, std::memory_order_relaxed);

  previewThreadPool.enqueue([chunkPos, revision, sourceSnapshot, state]() {
    if (!state) {
      return;
    }

    const auto finishTask = [&state]() {
      state->queuedChunkGenerations.fetch_sub(1, std::memory_order_relaxed);
    };

    if (!state->acceptingResults.load(std::memory_order_acquire) || !sourceSnapshot) {
      finishTask();
      return;
    }

    auto readyChunk = std::make_unique<ReadyChunkResult>();
    readyChunk->pos = chunkPos;
    readyChunk->revision = revision;

    Chunk temp(chunkPos);
    sourceSnapshot->generateChunk(temp);
    std::memcpy(readyChunk->blocks, temp.blocks, sizeof(temp.blocks));

    if (!state->acceptingResults.load(std::memory_order_acquire)) {
      finishTask();
      return;
    }

    {
      std::lock_guard<std::mutex> lock(state->readyMutex);
      if (state->acceptingResults.load(std::memory_order_relaxed)) {
        state->readyResults.push_back(std::move(readyChunk));
      }
    }

    finishTask();
  });
}

void PreviewWorldController::unloadOutsideTarget(
    const std::unordered_set<glm::ivec3, IVec3Hash>& targetChunks) {
  std::vector<glm::ivec3> toRemove;
  toRemove.reserve(chunks_.size());
  for (const auto& [chunkPos, entry] : chunks_) {
    (void)entry;
    if (targetChunks.find(chunkPos) == targetChunks.end()) {
      toRemove.push_back(chunkPos);
    }
  }

  for (const glm::ivec3& chunkPos : toRemove) {
    chunks_.erase(chunkPos);
  }
}

void PreviewWorldController::rebuildDirtyMeshes(const glm::ivec3& centerChunk) {
  struct DirtyCandidate {
    glm::ivec3 pos{0};
    int distance2 = 0;
  };

  if (dirtyChunkQueue_.empty()) {
    return;
  }

  const int dequeueBudget = std::max(maxMeshesPerFrame_ * 8, maxMeshesPerFrame_);
  std::vector<DirtyCandidate> candidates;
  candidates.reserve(dequeueBudget);

  int dequeued = 0;
  while (dequeued < dequeueBudget && !dirtyChunkQueue_.empty()) {
    const glm::ivec3 chunkPos = dirtyChunkQueue_.front();
    dirtyChunkQueue_.pop_front();
    dequeued++;

    auto chunkIt = chunks_.find(chunkPos);
    if (chunkIt == chunks_.end() || !chunkIt->second.chunk) {
      continue;
    }

    Chunk* chunk = chunkIt->second.chunk.get();
    chunk->dirtyQueued = false;
    if (!chunk->dirty || !chunk->generated.load(std::memory_order_relaxed)) {
      continue;
    }

    const glm::ivec3 delta = chunkPos - centerChunk;
    candidates.push_back(
        DirtyCandidate{chunkPos, delta.x * delta.x + delta.y * delta.y + delta.z * delta.z});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const DirtyCandidate& a, const DirtyCandidate& b) {
              return a.distance2 < b.distance2;
            });

  int meshedThisFrame = 0;
  for (const DirtyCandidate& candidate : candidates) {
    auto chunkIt = chunks_.find(candidate.pos);
    if (chunkIt == chunks_.end() || !chunkIt->second.chunk) {
      continue;
    }

    Chunk* chunk = chunkIt->second.chunk.get();
    if (!chunk->dirty || !chunk->generated.load(std::memory_order_relaxed)) {
      continue;
    }

    if (meshedThisFrame >= maxMeshesPerFrame_) {
      markDirty(candidate.pos);
      continue;
    }

    auto readyNeighbor = [this](const glm::ivec3& position) -> Chunk* {
      auto neighborIt = chunks_.find(position);
      if (neighborIt == chunks_.end() || !neighborIt->second.chunk) {
        return nullptr;
      }
      Chunk* neighbor = neighborIt->second.chunk.get();
      return neighbor->generated.load(std::memory_order_relaxed) ? neighbor : nullptr;
    };

    Chunk* nx = readyNeighbor(candidate.pos + glm::ivec3(-1, 0, 0));
    Chunk* px = readyNeighbor(candidate.pos + glm::ivec3(1, 0, 0));
    Chunk* ny = readyNeighbor(candidate.pos + glm::ivec3(0, -1, 0));
    Chunk* py = readyNeighbor(candidate.pos + glm::ivec3(0, 1, 0));
    Chunk* nz = readyNeighbor(candidate.pos + glm::ivec3(0, 0, -1));
    Chunk* pz = readyNeighbor(candidate.pos + glm::ivec3(0, 0, 1));

    chunk->buildMesh(depth(), nx, px, ny, py, nz, pz);
    meshedThisFrame++;
  }
}

void PreviewWorldController::rebuildStreamLoadOffsets() {
  streamOffsetsRenderDistance_ = streamRenderDistance_;
  streamLoadOffsets_.clear();

  const int radius = streamRenderDistance_;
  const int side = radius * 2 + 1;
  streamLoadOffsets_.reserve(static_cast<std::size_t>(side) * side * side);

  for (int x = -radius; x <= radius; x++) {
    for (int y = -radius; y <= radius; y++) {
      for (int z = -radius; z <= radius; z++) {
        streamLoadOffsets_.push_back(glm::ivec3(x, y, z));
      }
    }
  }

  std::sort(streamLoadOffsets_.begin(), streamLoadOffsets_.end(),
            [](const glm::ivec3& a, const glm::ivec3& b) {
              const int da = a.x * a.x + a.y * a.y + a.z * a.z;
              const int db = b.x * b.x + b.y * b.y + b.z * b.z;
              if (da != db) {
                return da < db;
              }
              if (a.y != b.y) {
                return std::abs(a.y) < std::abs(b.y);
              }
              if (a.x != b.x) {
                return std::abs(a.x) < std::abs(b.x);
              }
              return std::abs(a.z) < std::abs(b.z);
            });
}

void PreviewWorldController::markDirty(const glm::ivec3& chunkPos) {
  auto chunkIt = chunks_.find(chunkPos);
  if (chunkIt == chunks_.end() || !chunkIt->second.chunk) {
    return;
  }

  Chunk* chunk = chunkIt->second.chunk.get();
  chunk->dirty = true;
  if (chunk->dirtyQueued) {
    return;
  }

  dirtyChunkQueue_.push_back(chunkPos);
  chunk->dirtyQueued = true;
}

void PreviewWorldController::markNeighborShellDirty(const glm::ivec3& chunkPos) {
  static const glm::ivec3 kOffsets[6] = {
      {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
      {0, 1, 0},  {0, 0, -1}, {0, 0, 1},
  };

  for (const glm::ivec3& offset : kOffsets) {
    markDirty(chunkPos + offset);
  }
}

PreviewWorldController::ViewFrustum
PreviewWorldController::ViewFrustum::fromViewProjection(
    const glm::mat4& viewProjection) {
  const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0],
                       viewProjection[2][0], viewProjection[3][0]);
  const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1],
                       viewProjection[2][1], viewProjection[3][1]);
  const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2],
                       viewProjection[2][2], viewProjection[3][2]);
  const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3],
                       viewProjection[2][3], viewProjection[3][3]);

  ViewFrustum frustum;
  frustum.planes[0] = row3 + row0;
  frustum.planes[1] = row3 - row0;
  frustum.planes[2] = row3 + row1;
  frustum.planes[3] = row3 - row1;
  frustum.planes[4] = row3 + row2;
  frustum.planes[5] = row3 - row2;

  for (glm::vec4& plane : frustum.planes) {
    const float length = glm::length(glm::vec3(plane.x, plane.y, plane.z));
    if (length > 1e-5f) {
      plane /= length;
    }
  }

  return frustum;
}

bool PreviewWorldController::ViewFrustum::intersectsAabb(
    const glm::vec3& minCorner, const glm::vec3& maxCorner) const {
  for (const glm::vec4& plane : planes) {
    const glm::vec3 positiveVertex(
        plane.x >= 0.0f ? maxCorner.x : minCorner.x,
        plane.y >= 0.0f ? maxCorner.y : minCorner.y,
        plane.z >= 0.0f ? maxCorner.z : minCorner.z);

    if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f) {
      return false;
    }
  }
  return true;
}

} // namespace BiomeMaker
