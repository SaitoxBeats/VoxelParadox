// Arquivo: VoxelParadox.Client/src/World/world/biome_registry.cpp
// Papel: implementa "biome registry" dentro do subsistema "world" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma region Includes
#include "biome_registry.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

#include "path/app_paths.hpp"
#pragma endregion

#pragma region BiomeRegistryImplementation
namespace {

// Funcao: verifica 'hasBiomePresetExtension' no registro de presets de biome.
// Detalhe: usa 'path' para avaliar a condicao consultada com base no estado atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool hasBiomePresetExtension(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    constexpr const char* suffix = ".fvbiome.json";
    const std::size_t suffixLength = 13;
    return filename.size() >= suffixLength &&
           filename.compare(filename.size() - suffixLength, suffixLength, suffix) == 0;
}

} // namespace

// Funcao: executa 'instance' no registro de presets de biome.
// Detalhe: centraliza a logica necessaria para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'BiomeRegistry&' para dar acesso direto ao objeto resolvido por esta rotina.
BiomeRegistry& BiomeRegistry::instance() {
    static BiomeRegistry registry;
    return registry;
}

// Funcao: executa 'reload' no registro de presets de biome.
// Detalhe: usa 'directory' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool BiomeRegistry::reload(const std::filesystem::path& directory) {
    presetEntries.clear();

    std::error_code ec;
    const std::filesystem::path resolvedDirectory = AppPaths::resolve(directory);
    if (!std::filesystem::exists(resolvedDirectory, ec)) {
        std::printf("[BiomePreset] Directory not found: %s\n",
                    resolvedDirectory.string().c_str());
        return false;
    }

    std::vector<std::filesystem::path> presetPaths;
    for (const auto& entry : std::filesystem::directory_iterator(resolvedDirectory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) continue;
        if (!hasBiomePresetExtension(entry.path())) continue;
        presetPaths.push_back(entry.path());
    }

    std::sort(presetPaths.begin(), presetPaths.end());
    std::printf("[BiomePreset][Registry] Found %zu preset file(s).\n",
                presetPaths.size());
    std::fflush(stdout);

    bool loadedAny = false;
    for (const std::filesystem::path& path : presetPaths) {
        std::printf("[BiomePreset][Registry] Loading %s\n",
                    path.filename().string().c_str());
        std::fflush(stdout);
        auto preset = std::make_shared<VoxelGame::BiomePreset>();
        std::string error;
        if (!VoxelGame::loadBiomePresetFromFile(path, *preset, error)) {
            std::printf("[BiomePreset] Ignored %s | %s\n", path.filename().string().c_str(),
                        error.c_str());
            continue;
        }

        const auto duplicate = std::find_if(
            presetEntries.begin(), presetEntries.end(),
            [&preset](const PresetEntry& entry) {
                return entry.selection.presetId == preset->id;
            });
        if (duplicate != presetEntries.end()) {
            std::printf("[BiomePreset] Ignored %s | duplicate preset id '%s'\n",
                        path.filename().string().c_str(), preset->id.c_str());
            continue;
        }

        PresetEntry entry;
        entry.selection = BiomeSelection::preset(preset->id, preset->displayName);
        entry.preset = std::move(preset);
        entry.sourcePath = path;
        presetEntries.push_back(std::move(entry));
        std::printf("[BiomePreset][Registry] Loaded %s\n",
                    path.filename().string().c_str());
        std::fflush(stdout);
        loadedAny = true;
    }

    std::printf("[BiomePreset] Loaded %zu preset(s) from %s\n", presetEntries.size(),
                resolvedDirectory.string().c_str());
    return loadedAny;
}

// Funcao: limpa 'clear' no registro de presets de biome.
// Detalhe: centraliza a logica necessaria para resetar o estado temporario mantido por este componente.
void BiomeRegistry::clear() {
    presetEntries.clear();
}

// Funcao: monta 'buildSelectableBiomes' no registro de presets de biome.
// Detalhe: centraliza a logica necessaria para derivar e compor um valor pronto para a proxima etapa do pipeline.
// Retorno: devolve 'std::vector<BiomeSelection>' com a colecao ou o resultado agregado montado por esta etapa.
std::vector<BiomeSelection> BiomeRegistry::buildSelectableBiomes() const {
    std::vector<BiomeSelection> selections;
    selections.reserve(presetEntries.size());
    for (const PresetEntry& entry : presetEntries) {
        selections.push_back(entry.selection);
    }

    return selections;
}

// Funcao: procura 'findPreset' no registro de presets de biome.
// Detalhe: usa 'presetId' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'std::shared_ptr<const VoxelGame::BiomePreset>' com o resultado composto por esta chamada.
std::shared_ptr<const VoxelGame::BiomePreset> BiomeRegistry::findPreset(
    const std::string& presetId) const {
    for (const PresetEntry& entry : presetEntries) {
        if (entry.selection.presetId == presetId) {
            return entry.preset;
        }
    }
    return nullptr;
}
#pragma endregion
