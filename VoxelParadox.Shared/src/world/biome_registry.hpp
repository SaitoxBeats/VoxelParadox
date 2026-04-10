// Arquivo: VoxelParadox.Client/src/World/world/biome_registry.hpp
// Papel: declara "biome registry" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "biome.hpp"
#include "biome_preset.hpp"
#pragma endregion

#pragma region BiomeRegistryApi
class BiomeRegistry {
public:
    struct PresetEntry {
        BiomeSelection selection;
        std::shared_ptr<VoxelGame::BiomePreset> preset;
        std::filesystem::path sourcePath;
    };

    // Funcao: executa 'instance' no registro de presets de biome.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'BiomeRegistry&' para dar acesso direto ao objeto resolvido por esta rotina.
    static BiomeRegistry& instance();

    // Funcao: executa 'reload' no registro de presets de biome.
    // Detalhe: usa 'directory' para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
    bool reload(const std::filesystem::path& directory);
    // Funcao: limpa 'clear' no registro de presets de biome.
    // Detalhe: centraliza a logica necessaria para resetar o estado temporario mantido por este componente.
    void clear();

    // Funcao: executa 'presets' no registro de presets de biome.
    // Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
    // Retorno: devolve 'const std::vector<PresetEntry>&' com a colecao ou o resultado agregado montado por esta etapa.
    const std::vector<PresetEntry>& presets() const { return presetEntries; }
    // Funcao: monta 'buildSelectableBiomes' no registro de presets de biome.
    // Detalhe: centraliza a logica necessaria para derivar e compor um valor pronto para a proxima etapa do pipeline.
    // Retorno: devolve 'std::vector<BiomeSelection>' com a colecao ou o resultado agregado montado por esta etapa.
    std::vector<BiomeSelection> buildSelectableBiomes() const;
    // Funcao: procura 'findPreset' no registro de presets de biome.
    // Detalhe: usa 'presetId' para localizar o primeiro elemento que atende ao criterio esperado.
    // Retorno: devolve 'std::shared_ptr<const VoxelGame::BiomePreset>' com o resultado composto por esta chamada.
    std::shared_ptr<const VoxelGame::BiomePreset> findPreset(
        const std::string& presetId) const;

private:
    std::vector<PresetEntry> presetEntries;
};
#pragma endregion
