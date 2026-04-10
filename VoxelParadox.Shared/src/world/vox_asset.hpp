// Arquivo: VoxelParadox.Client/src/World/world/vox_asset.hpp
// Papel: declara "vox asset" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "path/app_paths.hpp"

#include "block.hpp"
#pragma endregion

#pragma region VoxAssetApi
struct VoxStructureVoxel {
    glm::ivec3 position{0};
    BlockType block = BlockType::AIR;
};

struct VoxStructureData {
    glm::ivec3 size{0};
    std::vector<VoxStructureVoxel> voxels;
    bool valid = false;
};

enum class VoxColorMapping : std::uint8_t {
    DEFAULT = 0,
    ANCIENT_RUINS = 1,
};

// Funcao: executa 'readVoxU32' no carregamento de assets VOX.
// Detalhe: usa 'file' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'std::uint32_t' com o valor numerico calculado para a proxima decisao do pipeline.
inline std::uint32_t readVoxU32(std::ifstream& file) {
    std::uint32_t value = 0;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

// Funcao: executa 'readVoxI32' no carregamento de assets VOX.
// Detalhe: usa 'file' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'std::int32_t' com o resultado composto por esta chamada.
inline std::int32_t readVoxI32(std::ifstream& file) {
    std::int32_t value = 0;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

// Funcao: executa 'voxColorToVec3' no carregamento de assets VOX.
// Detalhe: usa 'rgba' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
inline glm::vec3 voxColorToVec3(std::uint32_t rgba) {
    const float r = static_cast<float>(rgba & 0xFFu) / 255.0f;
    const float g = static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
    return glm::vec3(r, g, b);
}

// Funcao: mapeia 'mapVoxColorToNearestBlock' no carregamento de assets VOX.
// Detalhe: usa 'color' para converter um valor de origem para a categoria usada pelo projeto.
// Retorno: devolve 'BlockType' com o resultado composto por esta chamada.
inline BlockType mapVoxColorToNearestBlock(const glm::vec3& color) {
    static const BlockType candidates[] = {
        BlockType::STONE,
        BlockType::CRYSTAL,
        BlockType::VOID_MATTER,
        BlockType::MEMBRANE,
        BlockType::ORGANIC,
        BlockType::METAL,
    };

    float bestDist = std::numeric_limits<float>::max();
    BlockType bestType = BlockType::STONE;
    for (BlockType candidate : candidates) {
        const glm::vec3 base = getBaseColor(candidate);
        const glm::vec3 diff = color - base;
        const float dist = glm::dot(diff, diff);
        if (dist < bestDist) {
            bestDist = dist;
            bestType = candidate;
        }
    }
    return bestType;
}

// Funcao: mapeia 'mapAncientRuinsVoxColorToBlock' no carregamento de assets VOX.
// Detalhe: usa 'rgba' para converter um valor de origem para a categoria usada pelo projeto.
// Retorno: devolve 'BlockType' com o resultado composto por esta chamada.
inline BlockType mapAncientRuinsVoxColorToBlock(std::uint32_t rgba) {
    const glm::vec3 color = voxColorToVec3(rgba);
    const float maxChannel = std::max(color.r, std::max(color.g, color.b));
    const float minChannel = std::min(color.r, std::min(color.g, color.b));
    const float saturation = maxChannel - minChannel;
    const float brightness = (color.r + color.g + color.b) / 3.0f;

    if (color.g > color.r * 1.08f && color.g > color.b * 1.08f && color.g > 0.18f) {
        return brightness >= 0.32f ? BlockType::ORGANIC : BlockType::MEMBRANE;
    }

    if (saturation <= 0.14f) {
        return BlockType::STONE;
    }

    return mapVoxColorToNearestBlock(color);
}

// Funcao: mapeia 'mapVoxColorToBlock' no carregamento de assets VOX.
// Detalhe: usa 'rgba', 'mapping' para converter um valor de origem para a categoria usada pelo projeto.
// Retorno: devolve 'BlockType' com o resultado composto por esta chamada.
inline BlockType mapVoxColorToBlock(std::uint32_t rgba,
                                    VoxColorMapping mapping = VoxColorMapping::DEFAULT) {
    const std::uint8_t alpha = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    if (alpha < 8) {
        return BlockType::AIR;
    }

    if (mapping == VoxColorMapping::ANCIENT_RUINS) {
        return mapAncientRuinsVoxColorToBlock(rgba);
    }

    return mapVoxColorToNearestBlock(voxColorToVec3(rgba));
}

// Funcao: retorna 'getVoxFilesRecursive' no carregamento de assets VOX.
// Detalhe: usa 'root' para expor um dado derivado ou um acesso controlado ao estado interno.
// Retorno: devolve 'std::vector<std::string>' com o texto pronto para exibicao, lookup ou serializacao.
inline std::vector<std::string> getVoxFilesRecursive(const std::string& root) {
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, std::shared_ptr<const std::vector<std::string>>> cache;
    const std::string resolvedRoot = AppPaths::resolveString(root);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto cached = cache.find(resolvedRoot);
        if (cached != cache.end() && cached->second) {
            return *cached->second;
        }
    }

    std::vector<std::string> files;
    const std::filesystem::path rootPath(resolvedRoot);
    std::error_code ec;
    if (std::filesystem::exists(rootPath, ec)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(rootPath, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".vox") continue;
            files.push_back(entry.path().generic_string());
        }
    }

    std::sort(files.begin(), files.end());
    auto sharedFiles =
        std::make_shared<const std::vector<std::string>>(std::move(files));
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto [it, inserted] = cache.emplace(resolvedRoot, sharedFiles);
        (void)inserted;
        return *(it->second);
    }
}

// Funcao: carrega 'loadVoxStructure' no carregamento de assets VOX.
// Detalhe: usa 'path', 'mapping' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'const VoxStructureData*' para dar acesso direto ao objeto resolvido por esta rotina.
inline const VoxStructureData* loadVoxStructure(
    const std::string& path,
    VoxColorMapping mapping = VoxColorMapping::DEFAULT,
    std::optional<BlockType> defaultBlock = std::nullopt) {
  static std::mutex cacheMutex;
  static std::unordered_map<std::string, std::shared_ptr<VoxStructureData>> cache;
  const std::string resolvedPath = AppPaths::resolveString(path);
  const std::string cacheKey =
        resolvedPath + "#" + std::to_string(static_cast<int>(mapping)) + "#" +
        (defaultBlock.has_value() ? std::to_string(static_cast<int>(*defaultBlock))
                                  : std::string("-"));
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto cached = cache.find(cacheKey);
        if (cached != cache.end() && cached->second) {
            return cached->second->valid ? cached->second.get() : nullptr;
        }
    }

    auto data = std::make_shared<VoxStructureData>();
    std::ifstream file(resolvedPath, std::ios::binary);
    if (!file.is_open()) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[cacheKey] = data;
        return nullptr;
    }

    char magic[4] = {};
    file.read(magic, 4);
    if (std::string(magic, 4) != "VOX ") {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[cacheKey] = data;
        return nullptr;
    }

    const std::uint32_t version = readVoxU32(file);
    (void)version;

    char mainId[4] = {};
    file.read(mainId, 4);
    if (std::string(mainId, 4) != "MAIN") {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[cacheKey] = data;
        return nullptr;
    }

    const std::uint32_t mainContentSize = readVoxU32(file);
    const std::uint32_t mainChildrenSize = readVoxU32(file);
    file.seekg(mainContentSize, std::ios::cur);
    const std::streamoff mainEnd =
        static_cast<std::streamoff>(file.tellg()) + static_cast<std::streamoff>(mainChildrenSize);

    struct RawVoxel {
        std::uint8_t x = 0;
        std::uint8_t y = 0;
        std::uint8_t z = 0;
        std::uint8_t colorIndex = 0;
    };

    glm::ivec3 rawSize(0);
    std::vector<RawVoxel> rawVoxels;
    std::array<std::uint32_t, 256> palette{};
    bool hasPalette = false;

    while (file && file.tellg() < mainEnd) {
        char chunkIdBytes[4] = {};
        file.read(chunkIdBytes, 4);
        if (!file) break;

        const std::string chunkId(chunkIdBytes, 4);
        const std::uint32_t contentSize = readVoxU32(file);
        const std::uint32_t childrenSize = readVoxU32(file);
        const std::streamoff contentStart = file.tellg();

        if (chunkId == "SIZE" && contentSize >= 12) {
            rawSize.x = readVoxI32(file);
            rawSize.y = readVoxI32(file);
            rawSize.z = readVoxI32(file);
        } else if (chunkId == "XYZI" && contentSize >= 4) {
            const std::uint32_t voxelCount = readVoxU32(file);
            rawVoxels.reserve(voxelCount);
            for (std::uint32_t i = 0; i < voxelCount; i++) {
                RawVoxel voxel;
                voxel.x = static_cast<std::uint8_t>(file.get());
                voxel.y = static_cast<std::uint8_t>(file.get());
                voxel.z = static_cast<std::uint8_t>(file.get());
                voxel.colorIndex = static_cast<std::uint8_t>(file.get());
                rawVoxels.push_back(voxel);
            }
        } else if (chunkId == "RGBA" && contentSize >= 1024) {
            hasPalette = true;
            for (int i = 1; i < 256; i++) {
                std::uint8_t r = static_cast<std::uint8_t>(file.get());
                std::uint8_t g = static_cast<std::uint8_t>(file.get());
                std::uint8_t b = static_cast<std::uint8_t>(file.get());
                std::uint8_t a = static_cast<std::uint8_t>(file.get());
                palette[i] = static_cast<std::uint32_t>(r) |
                             (static_cast<std::uint32_t>(g) << 8) |
                             (static_cast<std::uint32_t>(b) << 16) |
                             (static_cast<std::uint32_t>(a) << 24);
            }
        }

        file.seekg(contentStart + static_cast<std::streamoff>(contentSize) +
                   static_cast<std::streamoff>(childrenSize));
    }

    if (!hasPalette) {
        for (int i = 1; i < 256; i++) {
            const std::uint8_t tone = static_cast<std::uint8_t>(i);
            palette[i] = static_cast<std::uint32_t>(tone) |
                         (static_cast<std::uint32_t>(tone) << 8) |
                         (static_cast<std::uint32_t>(tone) << 16) |
                         (0xFFu << 24);
        }
    }

    if (!rawVoxels.empty()) {
        data->size = glm::ivec3(rawSize.x, rawSize.z, rawSize.y);
        data->voxels.reserve(rawVoxels.size());
        for (const RawVoxel& raw : rawVoxels) {
            if (raw.colorIndex == 0) continue;
            const BlockType block = defaultBlock.has_value()
                                        ? *defaultBlock
                                        : mapVoxColorToBlock(palette[raw.colorIndex],
                                                             mapping);
            if (block == BlockType::AIR) continue;

            // MagicaVoxel uses Z-up; the game uses Y-up.
            // Mirror X as well so the imported structure keeps the same handedness in-game.
            data->voxels.push_back({
                glm::ivec3(rawSize.x - 1 - static_cast<int>(raw.x),
                           static_cast<int>(raw.z),
                           static_cast<int>(raw.y)),
                block
            });
        }
        data->valid = !data->voxels.empty();
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto [it, inserted] = cache.emplace(cacheKey, data);
        (void)inserted;
        return it->second && it->second->valid ? it->second.get() : nullptr;
    }
}
#pragma endregion
