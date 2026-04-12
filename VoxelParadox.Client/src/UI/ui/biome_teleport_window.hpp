// Arquivo: VoxelParadox.Client/src/UI/ui/biome_teleport_window.hpp
// Papel: declara "biome teleport window" dentro do subsistema "ui" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#pragma region Includes
#include <vector>

#include "world/biome/biome.hpp"
#pragma endregion

#pragma region BiomeTeleportWindowApi
namespace BiomeTeleportWindow {

// Funcao: define 'setAvailableBiomes' na janela de teleporte de biomes.
// Detalhe: usa 'biomes' para aplicar ao componente o valor ou configuracao recebida.
void setAvailableBiomes(const std::vector<BiomeSelection>& biomes);
// Funcao: define 'setSelectedBiome' na janela de teleporte de biomes.
// Detalhe: usa 'biome' para aplicar ao componente o valor ou configuracao recebida.
void setSelectedBiome(const BiomeSelection& biome);
// Funcao: executa 'syncCurrentBiome' na janela de teleporte de biomes.
// Detalhe: usa 'biome' para encapsular esta etapa especifica do subsistema.
void syncCurrentBiome(const BiomeSelection& biome);
// Funcao: renderiza 'draw' na janela de teleporte de biomes.
// Detalhe: centraliza a logica necessaria para desenhar a saida visual correspondente usando o estado atual.
void draw();
// Funcao: libera 'shutdown' na janela de teleporte de biomes.
// Detalhe: centraliza a logica necessaria para encerrar a etapa e descartar recursos associados.
void shutdown();
// Funcao: consome 'consumeTeleportRequest' na janela de teleporte de biomes.
// Detalhe: usa 'outBiome' para retirar um pedido ou evento pendente para evitar reprocessamento.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool consumeTeleportRequest(BiomeSelection& outBiome);
// Funcao: consome 'consumeUpdateLocationRequest' na janela de teleporte de biomes.
// Detalhe: centraliza a logica necessaria para retirar um pedido ou evento pendente para evitar reprocessamento.
// Retorno: devolve 'bool' para indicar sucesso, presenca, validacao ou qualquer outra condicao relevante produzida pela chamada.
bool consumeUpdateLocationRequest();

} // namespace BiomeTeleportWindow
#pragma endregion
