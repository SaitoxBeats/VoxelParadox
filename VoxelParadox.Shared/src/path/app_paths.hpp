// Arquivo: Engine/src/path/app_paths.h
// Papel: declara um componente compartilhado do engine.
// Fluxo: é reutilizado pelos alvos do jogo e do editor.
// Dependências principais: headers e bibliotecas compartilhadas do projeto.
#pragma once

// Arquivo: Engine/src/path/app_paths.h
// Papel: declara a camada Ãºnica de resoluÃ§Ã£o de caminhos do projeto para runtime, editor e testes.
// Fluxo: os chamadores pedem caminhos lÃ³gicos como `Assets/...`, `World/...` ou `Saves/...`
// e recebem o caminho fÃ­sico correto no workspace de desenvolvimento ou no pacote staged.
// DependÃªncias principais: `std::filesystem`.

#include <filesystem>
#include <string>

namespace AppPaths {

bool initialize();
bool initializeFromExecutable(const std::filesystem::path& executablePath);

const std::filesystem::path& executablePath();
const std::filesystem::path& executableDirectory();
const std::filesystem::path& workspaceRoot();
const std::filesystem::path& resourcesRoot();
const std::filesystem::path& engineRoot();

std::filesystem::path sharedRoot();
std::filesystem::path assetsRoot();
std::filesystem::path gameDataRoot();
std::filesystem::path fontsRoot();
std::filesystem::path texturesRoot();
std::filesystem::path hudTexturesRoot();
std::filesystem::path itemTexturesRoot();
std::filesystem::path recipesRoot();
std::filesystem::path shadersRoot();
std::filesystem::path voxsRoot();
std::filesystem::path savesRoot();
std::filesystem::path saveWorldsRoot();
std::filesystem::path biomeMakerSavesRoot();
std::filesystem::path seedsRoot();
std::filesystem::path worldRoot();
std::filesystem::path biomePresetsRoot();
std::filesystem::path modulesRoot();

std::filesystem::path workspaceFile(const std::filesystem::path& relativePath);
std::filesystem::path resolve(const std::filesystem::path& path);
std::string resolveString(const std::filesystem::path& path);

} // namespace AppPaths
