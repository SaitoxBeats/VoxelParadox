// Arquivo: VoxelParadox.Client/src/World/world/generation/world_generator.hpp
// Papel: declara "world generator" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <cstdint>
#include <memory>

#include "world/biome/biome_preset.hpp"
#include "world/generation/chunk.hpp"
#include "world/generation/chunk_generator_source.hpp"
#pragma endregion

#pragma region WorldGeneratorApi
class WorldGenerator {
public:
    int depth = 0;
    std::uint32_t seed = 42;
    std::shared_ptr<const VoxelGame::BiomePreset> biomePreset;

    // Funcao: executa 'WorldGenerator' na interface de geracao de mundo.
    // Detalhe: usa 'depth', 'seed', 'biomePreset' para encapsular esta etapa especifica do subsistema.
    WorldGenerator(int depth, std::uint32_t seed,
                   std::shared_ptr<const VoxelGame::BiomePreset> biomePreset)
        : depth(depth), seed(seed), biomePreset(std::move(biomePreset)) {
        if (!this->biomePreset) {
            return;
        }

        generatorSource_ =
            std::make_shared<VoxelGame::PresetModuleGeneratorSource>(
                this->biomePreset, depth, generationSeed(seed, depth));
    }

    // Funcao: executa 'generate' na interface de geracao de mundo.
    // Detalhe: usa 'chunk' para encapsular esta etapa especifica do subsistema.
    void generate(Chunk& chunk) const {
        if (!generatorSource_) {
            for (int x = 0; x < Chunk::SIZE; x++)
            for (int y = 0; y < Chunk::SIZE; y++)
            for (int z = 0; z < Chunk::SIZE; z++) {
                chunk.blocks[x][y][z] = BlockIds::AIR;
            }
            chunk.generated = true;
            return;
        }

        generatorSource_->generateChunk(chunk);
    }

private:
    std::shared_ptr<const VoxelGame::IChunkGeneratorSource> generatorSource_{};

    static std::uint32_t generationSeed(std::uint32_t seed, int depth) {
        const std::uint32_t depthValue =
            depth < 0 ? 0u : static_cast<std::uint32_t>(depth);
        if (depthValue == 0u) {
            return seed;
        }

        std::uint32_t mixed =
            seed ^ (0x9E3779B9u + depthValue * 0x85EBCA6Bu);
        mixed ^= mixed >> 16;
        mixed *= 0x7FEB352Du;
        mixed ^= mixed >> 15;
        mixed *= 0x846CA68Bu;
        mixed ^= mixed >> 16;
        return mixed;
    }
};
#pragma endregion
