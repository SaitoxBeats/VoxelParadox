// Arquivo: VoxelParadox.Client/src/World/world/biome.hpp
// Papel: declara "biome" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <cstdint>
#include <string>
#include <utility>
#pragma endregion

#pragma region BiomeApi
struct BiomeSelection {
    enum class Kind : std::uint8_t {
        PRESET = 1,
    };

    Kind kind = Kind::PRESET;
    std::string presetId;
    std::string displayName;
    std::string storageId;

    // Funcao: executa 'preset' neste modulo do projeto VoxelParadox.Client.
    // Detalhe: usa 'id', 'name' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'BiomeSelection' com o resultado composto por esta chamada.
    static BiomeSelection preset(std::string id, std::string name) {
        BiomeSelection selection;
        selection.presetId = std::move(id);
        selection.displayName = std::move(name);
        selection.storageId = "preset_" + selection.presetId;
        return selection;
    }

    // Funcao: verifica 'isPreset' neste modulo do projeto VoxelParadox.Client.
    // Detalhe: centraliza a logica necessaria para avaliar a condicao consultada com base no estado atual.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool isPreset() const { return true; }
    // Funcao: executa 'empty' neste modulo do projeto VoxelParadox.Client.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool empty() const { return presetId.empty(); }
};

// Funcao: executa 'operator==' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'lhs', 'rhs' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool operator==(const BiomeSelection& lhs, const BiomeSelection& rhs) {
    return lhs.kind == rhs.kind && lhs.presetId == rhs.presetId &&
           lhs.displayName == rhs.displayName && lhs.storageId == rhs.storageId;
}

// Funcao: executa 'operator!=' neste modulo do projeto VoxelParadox.Client.
// Detalhe: usa 'lhs', 'rhs' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
inline bool operator!=(const BiomeSelection& lhs, const BiomeSelection& rhs) {
    return !(lhs == rhs);
}
#pragma endregion
