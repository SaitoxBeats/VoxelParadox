// Arquivo: VoxelParadox.Client/src/World/world/voxel_assets/vox_structure.hpp
// Papel: declara "vox structure" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include "world/generation/fractal_world.hpp"
#include "world/voxel_assets/vox_asset.hpp"
#pragma endregion

#pragma region VoxStructureApi
// Funcao: executa 'stampVoxStructure' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'world', 'structure', 'origin' para encapsular esta etapa especifica do subsistema.
inline void stampVoxStructure(FractalWorld& world, const VoxStructureData& structure,
                              const glm::ivec3& origin) {
    world.modifications.reserve(world.modifications.size() + structure.voxels.size());
    for (const VoxStructureVoxel& voxel : structure.voxels) {
        world.modifications[origin + voxel.position] = voxel.block;
    }
}
#pragma endregion
