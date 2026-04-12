// Arquivo: VoxelParadox.Client/src/Systems/debug/biome_debug_tools.hpp
// Papel: declara "biome debug tools" dentro do subsistema "debug" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once


#include <cstdint>

#include <glm/glm.hpp>

#include "world/biome/biome.hpp"


class Player;
class WorldStack;
class hudPortalInfo;

namespace DebugBiomeTools {

// Funcao: procura 'findBestSpawnLocation' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'spawnAnchor', 'player' para localizar o primeiro elemento que atende ao criterio esperado.
// Retorno: devolve 'glm::vec3' com o resultado composto por esta chamada.
glm::vec3 findBestSpawnLocation(WorldStack& worldStack, const glm::vec3& spawnAnchor,
                                const Player& player);
// Funcao: atualiza 'updatePlayerLocationAroundPlayer' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'player' para sincronizar o estado derivado com o frame atual.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool updatePlayerLocationAroundPlayer(WorldStack& worldStack, Player& player);
// Funcao: executa 'teleportToBiome' nas ferramentas de debug de biome.
// Detalhe: usa 'worldStack', 'player', 'portalInfo', 'biomeSelection', 'rootSeed', 'spawnAnchor', 'currentTime', 'lastTime' para encapsular esta etapa especifica do subsistema.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool teleportToBiome(WorldStack& worldStack, Player& player, hudPortalInfo* portalInfo,
                     const BiomeSelection& biomeSelection, std::uint32_t rootSeed,
                     const glm::vec3& spawnAnchor, double currentTime,
                     double& lastTime);

} // namespace DebugBiomeTools
