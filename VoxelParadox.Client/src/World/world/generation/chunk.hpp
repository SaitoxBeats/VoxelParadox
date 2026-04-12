// Arquivo: VoxelParadox.Client/src/World/world/generation/chunk.hpp
// Papel: declara "chunk" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>
#include <glad/glad.h>

#include "world/block/block.hpp"
#include "engine/meshing/greedy_mesh.hpp"
#pragma endregion

#pragma region ChunkApi
class Chunk {
public:
    static constexpr int SIZE = 16;
    using Vertex = ENGINE::Meshing::MeshVertex;

    struct CustomModelBlockInstance {
        glm::ivec3 localPos{0};
        BlockId type = BlockIds::AIR;
    };

    glm::ivec3 chunkPos;
    BlockId blocks[SIZE][SIZE][SIZE];
    GLuint vao = 0;
    GLuint vbo = 0;
    int vertexCount = 0;
    bool dirty = false;
    bool dirtyQueued = false;
    std::atomic<bool> generated{false};
    std::atomic<bool> generating{false};

    Chunk(glm::ivec3 pos) : chunkPos(pos) {
        memset(blocks, 0, sizeof(blocks));
    }

    ~Chunk() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
    }

    BlockId getBlock(int x, int y, int z) const {
        if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE) {
            return BlockIds::AIR;
        }
        return blocks[x][y][z];
    }

    void setBlock(int x, int y, int z, BlockId type) {
        if (x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE) {
            blocks[x][y][z] = type;
        }
    }

    void buildMesh(int depth, Chunk* nxC, Chunk* pxC, Chunk* nyC, Chunk* pyC,
                   Chunk* nzC, Chunk* pzC, bool useGreedyMeshing = true) {
        const auto sampleBlock = [this, nxC, pxC, nyC, pyC, nzC, pzC](
                                     const glm::ivec3& localPos) -> BlockId {
            if (localPos.x >= 0 && localPos.x < SIZE && localPos.y >= 0 &&
                localPos.y < SIZE && localPos.z >= 0 && localPos.z < SIZE) {
                return blocks[localPos.x][localPos.y][localPos.z];
            }

            if (localPos.x < 0 && nxC) {
                return nxC->getBlock(SIZE - 1, localPos.y, localPos.z);
            }
            if (localPos.x >= SIZE && pxC) {
                return pxC->getBlock(0, localPos.y, localPos.z);
            }
            if (localPos.y < 0 && nyC) {
                return nyC->getBlock(localPos.x, SIZE - 1, localPos.z);
            }
            if (localPos.y >= SIZE && pyC) {
                return pyC->getBlock(localPos.x, 0, localPos.z);
            }
            if (localPos.z < 0 && nzC) {
                return nzC->getBlock(localPos.x, localPos.y, SIZE - 1);
            }
            if (localPos.z >= SIZE && pzC) {
                return pzC->getBlock(localPos.x, localPos.y, 0);
            }

            return BlockIds::AIR;
        };

        ENGINE::Meshing::GreedyChunkInput input{};
        input.chunkCoord = chunkPos;
        input.chunkOrigin =
            glm::vec3(chunkPos) * static_cast<float>(SIZE);
        input.chunkSize = SIZE;
        input.depth = depth;
        input.enableMerging = useGreedyMeshing;
        input.sampleBlock = sampleBlock;
        input.resolveFaceMaterial = [depth](BlockId block,
                                            int faceDirection) {
            ENGINE::Meshing::FaceMaterialDesc material{};
            material.color = getBlockColor(block, depth, faceDirection);
            material.material = getBlockMaterialId(block);
            material.transparent = !isSolid(block);
            return material;
        };

        ENGINE::Meshing::GreedyMeshResult meshResult =
            ENGINE::Meshing::buildGreedyChunkMesh(input);
        meshVertices = std::move(meshResult.vertices);
        rebuildCustomModelBlockInstances();
        vertexCount = static_cast<int>(meshVertices.size());

        uploadMeshVertices();
        dirty = false;
    }

    void render() const {
        if (vertexCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glBindVertexArray(0);
    }

    const std::vector<CustomModelBlockInstance>& customModelBlocks() const {
        return customModelBlocks_;
    }

private:
    void rebuildCustomModelBlockInstances() {
        customModelBlocks_.clear();
        for (int x = 0; x < SIZE; ++x) {
            for (int y = 0; y < SIZE; ++y) {
                for (int z = 0; z < SIZE; ++z) {
                    const BlockId type = blocks[x][y][z];
                    if (!usesCustomBlockModel(type)) {
                        continue;
                    }

                    customModelBlocks_.push_back(
                        CustomModelBlockInstance{glm::ivec3(x, y, z), type});
                }
            }
        }
    }

    void uploadMeshVertices() {
        if (!vao) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsetof(Vertex, position)));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsetof(Vertex, normal)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsetof(Vertex, color)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsetof(Vertex, ao)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsetof(Vertex, material)));
            glEnableVertexAttribArray(4);
        } else {
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
        }

        const std::size_t requiredBytes =
            meshVertices.size() * sizeof(Vertex);
        if (requiredBytes > vboCapacityBytes) {
            vboCapacityBytes =
                std::max(requiredBytes,
                         vboCapacityBytes == 0
                             ? std::size_t(4096 * sizeof(Vertex))
                             : vboCapacityBytes * 2);
            glBufferData(GL_ARRAY_BUFFER, vboCapacityBytes, nullptr,
                         GL_DYNAMIC_DRAW);
        }

        if (requiredBytes > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes,
                            meshVertices.data());
        }

        glBindVertexArray(0);
    }

    std::vector<Vertex> meshVertices;
    std::vector<CustomModelBlockInstance> customModelBlocks_;
    std::size_t vboCapacityBytes = 0;
};
#pragma endregion
