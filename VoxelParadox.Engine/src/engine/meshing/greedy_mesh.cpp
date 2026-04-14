#include "greedy_mesh.hpp"

#include <cstddef>

namespace ENGINE::Meshing {
namespace {

constexpr int kFaceCount = 6;

const glm::vec3 kFaceNormals[kFaceCount] = {
    {-1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, -1.0f},
    {0.0f, 0.0f, 1.0f},
};

const int kAoNeighbors[kFaceCount][4][3][3] = {
    {
        {{-1, 0, -1}, {-1, -1, 0}, {-1, -1, -1}},
        {{-1, 0, 1}, {-1, -1, 0}, {-1, -1, 1}},
        {{-1, 0, 1}, {-1, 1, 0}, {-1, 1, 1}},
        {{-1, 0, -1}, {-1, 1, 0}, {-1, 1, -1}},
    },
    {
        {{1, 0, 1}, {1, -1, 0}, {1, -1, 1}},
        {{1, 0, -1}, {1, -1, 0}, {1, -1, -1}},
        {{1, 0, -1}, {1, 1, 0}, {1, 1, -1}},
        {{1, 0, 1}, {1, 1, 0}, {1, 1, 1}},
    },
    {
        {{-1, -1, 0}, {0, -1, -1}, {-1, -1, -1}},
        {{1, -1, 0}, {0, -1, -1}, {1, -1, -1}},
        {{1, -1, 0}, {0, -1, 1}, {1, -1, 1}},
        {{-1, -1, 0}, {0, -1, 1}, {-1, -1, 1}},
    },
    {
        {{-1, 1, 0}, {0, 1, 1}, {-1, 1, 1}},
        {{1, 1, 0}, {0, 1, 1}, {1, 1, 1}},
        {{1, 1, 0}, {0, 1, -1}, {1, 1, -1}},
        {{-1, 1, 0}, {0, 1, -1}, {-1, 1, -1}},
    },
    {
        {{1, 0, -1}, {0, -1, -1}, {1, -1, -1}},
        {{-1, 0, -1}, {0, -1, -1}, {-1, -1, -1}},
        {{-1, 0, -1}, {0, 1, -1}, {-1, 1, -1}},
        {{1, 0, -1}, {0, 1, -1}, {1, 1, -1}},
    },
    {
        {{-1, 0, 1}, {0, -1, 1}, {-1, -1, 1}},
        {{1, 0, 1}, {0, -1, 1}, {1, -1, 1}},
        {{1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
        {{-1, 0, 1}, {0, 1, 1}, {-1, 1, 1}},
    },
};

struct FaceKey {
    bool visible = false;
    FaceMaterialDesc material{};
    AoSignature ao{};
};

bool sameColor(const glm::vec4& a, const glm::vec4& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool sameFaceKey(const FaceKey& a, const FaceKey& b) {
    return a.visible == b.visible &&
           sameColor(a.material.color, b.material.color) &&
           a.material.material == b.material.material &&
           a.material.transparent == b.material.transparent &&
           a.ao == b.ao;
}

BlockId sampleBlockOrAir(const GreedyChunkInput& input, const glm::ivec3& localPos) {
    if (!input.sampleBlock) {
        return BlockIds::AIR;
    }
    return input.sampleBlock(localPos);
}

bool isRenderable(const GreedyChunkInput& input, BlockId block) {
    return input.isRenderableBlock ? input.isRenderableBlock(block) : isSolid(block);
}

bool isOccluding(const GreedyChunkInput& input, BlockId block) {
    return input.isOccludingBlock ? input.isOccludingBlock(block) : isSolid(block);
}

std::uint8_t computeAoLevel(const GreedyChunkInput& input, const glm::ivec3& blockPos,
                            int face, int vertexIndex) {
    const int* sideA = kAoNeighbors[face][vertexIndex][0];
    const int* sideB = kAoNeighbors[face][vertexIndex][1];
    const int* corner = kAoNeighbors[face][vertexIndex][2];

    const bool occA = isOccluding(
        input, sampleBlockOrAir(
                   input, blockPos + glm::ivec3(sideA[0], sideA[1], sideA[2])));
    const bool occB = isOccluding(
        input, sampleBlockOrAir(
                   input, blockPos + glm::ivec3(sideB[0], sideB[1], sideB[2])));
    const bool occCorner = isOccluding(
        input, sampleBlockOrAir(
                   input, blockPos + glm::ivec3(corner[0], corner[1], corner[2])));

    const int aoLevel =
        (occA && occB) ? 0 : 3 - static_cast<int>(occA) - static_cast<int>(occB) -
                                 static_cast<int>(occCorner);
    return static_cast<std::uint8_t>(glm::clamp(aoLevel, 0, 3));
}

AoSignature buildAoSignature(const GreedyChunkInput& input, const glm::ivec3& blockPos,
                             int face) {
    AoSignature signature{};
    for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex) {
        signature.levels[static_cast<std::size_t>(vertexIndex)] =
            computeAoLevel(input, blockPos, face, vertexIndex);
    }
    return signature;
}

glm::ivec3 faceBlockPos(int face, int slice, int u, int v) {
    switch (face) {
    case 0:
    case 1:
        return {slice, v, u};
    case 2:
    case 3:
        return {u, slice, v};
    case 4:
    case 5:
        return {u, v, slice};
    default:
        return {0, 0, 0};
    }
}

glm::ivec3 faceNeighborPos(int face, const glm::ivec3& blockPos) {
    switch (face) {
    case 0: return blockPos + glm::ivec3(-1, 0, 0);
    case 1: return blockPos + glm::ivec3(1, 0, 0);
    case 2: return blockPos + glm::ivec3(0, -1, 0);
    case 3: return blockPos + glm::ivec3(0, 1, 0);
    case 4: return blockPos + glm::ivec3(0, 0, -1);
    case 5: return blockPos + glm::ivec3(0, 0, 1);
    default: return blockPos;
    }
}

glm::vec3 quadCorner(int face, int slice, int u, int v, int width, int height,
                     int cornerIndex) {
    switch (face) {
    case 0: {
        const float x = static_cast<float>(slice);
        const float y0 = static_cast<float>(v);
        const float y1 = static_cast<float>(v + height);
        const float z0 = static_cast<float>(u);
        const float z1 = static_cast<float>(u + width);
        switch (cornerIndex) {
        case 0: return {x, y0, z0};
        case 1: return {x, y0, z1};
        case 2: return {x, y1, z1};
        default: return {x, y1, z0};
        }
    }
    case 1: {
        const float x = static_cast<float>(slice + 1);
        const float y0 = static_cast<float>(v);
        const float y1 = static_cast<float>(v + height);
        const float z0 = static_cast<float>(u);
        const float z1 = static_cast<float>(u + width);
        switch (cornerIndex) {
        case 0: return {x, y0, z1};
        case 1: return {x, y0, z0};
        case 2: return {x, y1, z0};
        default: return {x, y1, z1};
        }
    }
    case 2: {
        const float y = static_cast<float>(slice);
        const float x0 = static_cast<float>(u);
        const float x1 = static_cast<float>(u + width);
        const float z0 = static_cast<float>(v);
        const float z1 = static_cast<float>(v + height);
        switch (cornerIndex) {
        case 0: return {x0, y, z0};
        case 1: return {x1, y, z0};
        case 2: return {x1, y, z1};
        default: return {x0, y, z1};
        }
    }
    case 3: {
        const float y = static_cast<float>(slice + 1);
        const float x0 = static_cast<float>(u);
        const float x1 = static_cast<float>(u + width);
        const float z0 = static_cast<float>(v);
        const float z1 = static_cast<float>(v + height);
        switch (cornerIndex) {
        case 0: return {x0, y, z1};
        case 1: return {x1, y, z1};
        case 2: return {x1, y, z0};
        default: return {x0, y, z0};
        }
    }
    case 4: {
        const float z = static_cast<float>(slice);
        const float x0 = static_cast<float>(u);
        const float x1 = static_cast<float>(u + width);
        const float y0 = static_cast<float>(v);
        const float y1 = static_cast<float>(v + height);
        switch (cornerIndex) {
        case 0: return {x1, y0, z};
        case 1: return {x0, y0, z};
        case 2: return {x0, y1, z};
        default: return {x1, y1, z};
        }
    }
    case 5: {
        const float z = static_cast<float>(slice + 1);
        const float x0 = static_cast<float>(u);
        const float x1 = static_cast<float>(u + width);
        const float y0 = static_cast<float>(v);
        const float y1 = static_cast<float>(v + height);
        switch (cornerIndex) {
        case 0: return {x0, y0, z};
        case 1: return {x1, y0, z};
        case 2: return {x1, y1, z};
        default: return {x0, y1, z};
        }
    }
    default:
        return glm::vec3(0.0f);
    }
}

void appendQuad(std::vector<MeshVertex>& vertices, const glm::vec3& chunkOrigin, int face,
                const FaceMaterialDesc& material, const AoSignature& ao, int slice, int u,
                int v, int width, int height) {
    const glm::vec3 v0 = chunkOrigin + quadCorner(face, slice, u, v, width, height, 0);
    const glm::vec3 v1 = chunkOrigin + quadCorner(face, slice, u, v, width, height, 1);
    const glm::vec3 v2 = chunkOrigin + quadCorner(face, slice, u, v, width, height, 2);
    const glm::vec3 v3 = chunkOrigin + quadCorner(face, slice, u, v, width, height, 3);
    const glm::vec3 normal = kFaceNormals[face];

    const float ao0 = static_cast<float>(ao.levels[0]) / 3.0f;
    const float ao1 = static_cast<float>(ao.levels[1]) / 3.0f;
    const float ao2 = static_cast<float>(ao.levels[2]) / 3.0f;
    const float ao3 = static_cast<float>(ao.levels[3]) / 3.0f;

    const std::size_t base = vertices.size();
    vertices.resize(base + 6);
    MeshVertex* dst = vertices.data() + base;

    if (ao0 + ao2 > ao1 + ao3) {
        dst[0] = {v0, normal, material.color, ao0, material.material};
        dst[1] = {v1, normal, material.color, ao1, material.material};
        dst[2] = {v2, normal, material.color, ao2, material.material};
        dst[3] = {v0, normal, material.color, ao0, material.material};
        dst[4] = {v2, normal, material.color, ao2, material.material};
        dst[5] = {v3, normal, material.color, ao3, material.material};
    } else {
        dst[0] = {v1, normal, material.color, ao1, material.material};
        dst[1] = {v2, normal, material.color, ao2, material.material};
        dst[2] = {v3, normal, material.color, ao3, material.material};
        dst[3] = {v1, normal, material.color, ao1, material.material};
        dst[4] = {v3, normal, material.color, ao3, material.material};
        dst[5] = {v0, normal, material.color, ao0, material.material};
    }
}

} // namespace

GreedyMeshResult buildGreedyChunkMesh(const GreedyChunkInput& input) {
    GreedyMeshResult result;
    if (!input.sampleBlock || !input.resolveFaceMaterial || input.chunkSize <= 0) {
        return result;
    }

    const int size = input.chunkSize;
    result.vertices.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) *
                            static_cast<std::size_t>(size) * 3);

    std::vector<FaceKey> mask(static_cast<std::size_t>(size) *
                              static_cast<std::size_t>(size));

    for (int face = 0; face < kFaceCount; ++face) {
        for (int slice = 0; slice < size; ++slice) {
            for (int v = 0; v < size; ++v) {
                for (int u = 0; u < size; ++u) {
                    FaceKey key{};
                    const glm::ivec3 blockPos = faceBlockPos(face, slice, u, v);
                    const BlockId block = sampleBlockOrAir(input, blockPos);
                    if (isRenderable(input, block)) {
                        const BlockId neighbor =
                            sampleBlockOrAir(input, faceNeighborPos(face, blockPos));
                        if (!isOccluding(input, neighbor)) {
                            key.visible = true;
                            key.material = input.resolveFaceMaterial(block, face);
                            key.ao = buildAoSignature(input, blockPos, face);
                        }
                    }

                    mask[static_cast<std::size_t>(v * size + u)] = key;
                }
            }

            for (int v = 0; v < size; ++v) {
                for (int u = 0; u < size; ++u) {
                    FaceKey& key = mask[static_cast<std::size_t>(v * size + u)];
                    if (!key.visible) {
                        continue;
                    }

                    int width = 1;
                    if (input.enableMerging) {
                        while (u + width < size) {
                            const FaceKey& neighbor =
                                mask[static_cast<std::size_t>(v * size + (u + width))];
                            if (!sameFaceKey(key, neighbor)) {
                                break;
                            }
                            ++width;
                        }
                    }

                    int height = 1;
                    if (input.enableMerging) {
                        bool canExpand = true;
                        while (v + height < size && canExpand) {
                            for (int offsetU = 0; offsetU < width; ++offsetU) {
                                const FaceKey& neighbor = mask[static_cast<std::size_t>(
                                    (v + height) * size + (u + offsetU))];
                                if (!sameFaceKey(key, neighbor)) {
                                    canExpand = false;
                                    break;
                                }
                            }
                            if (canExpand) {
                                ++height;
                            }
                        }
                    }

                    appendQuad(result.vertices, input.chunkOrigin, face, key.material, key.ao,
                               slice, u, v, width, height);
                    result.stats.quadCount += 1;
                    result.stats.triangleCount += 2;
                    result.stats.mergedFaceCount += (width * height) - 1;

                    for (int clearV = 0; clearV < height; ++clearV) {
                        for (int clearU = 0; clearU < width; ++clearU) {
                            mask[static_cast<std::size_t>((v + clearV) * size + (u + clearU))] =
                                FaceKey{};
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace ENGINE::Meshing
