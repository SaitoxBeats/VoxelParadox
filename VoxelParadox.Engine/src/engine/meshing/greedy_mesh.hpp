#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "mesh_types.hpp"
#include "world/block.hpp"

namespace ENGINE::Meshing {

struct GreedyMeshStats {
    int quadCount = 0;
    int triangleCount = 0;
    int mergedFaceCount = 0;
};

struct GreedyMeshResult {
    std::vector<MeshVertex> vertices;
    GreedyMeshStats stats;
};

struct GreedyChunkInput {
    glm::ivec3 chunkCoord{0};
    glm::vec3 chunkOrigin{0.0f};
    int chunkSize = 16;
    int depth = 0;
    bool enableMerging = true;
    std::function<BlockId(const glm::ivec3& localPos)> sampleBlock;
    std::function<FaceMaterialDesc(BlockId block, int faceDirection)> resolveFaceMaterial;
};

GreedyMeshResult buildGreedyChunkMesh(const GreedyChunkInput& input);

} // namespace ENGINE::Meshing
