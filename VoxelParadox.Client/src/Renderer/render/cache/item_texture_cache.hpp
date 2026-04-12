// Arquivo: VoxelParadox.Client/src/Renderer/render/cache/item_texture_cache.hpp
// Papel: declara "item texture cache" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#pragma once

#include <glad/glad.h>

// Funcao: carrega 'loadTexture2DFromFile' no carregamento de texturas de itens.
// Detalhe: usa 'path', 'flipVertically' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'GLuint' com o valor numerico calculado para a proxima decisao do pipeline.
GLuint loadTexture2DFromFile(const char* path, bool flipVertically);

