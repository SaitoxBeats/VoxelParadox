// Arquivo: VoxelParadox.Client/src/Renderer/render/item_texture_cache.cpp
// Papel: implementa "item texture cache" dentro do subsistema "render" do projeto VoxelParadox.Client.
// Fluxo: concentra tipos, dados e rotinas usados por este ponto do runtime de forma documentada e consistente.
// Dependencias principais: os headers, tipos STL e bibliotecas externas incluidos logo abaixo.

#include "render/cache/item_texture_cache.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "path/app_paths.hpp"

// Funcao: carrega 'loadTexture2DFromFile' no carregamento de texturas de itens.
// Detalhe: usa 'path', 'flipVertically' para ler dados externos e adapta-los ao formato interno usado pelo jogo.
// Retorno: devolve 'GLuint' com o valor numerico calculado para a proxima decisao do pipeline.
GLuint loadTexture2DFromFile(const char* path, bool flipVertically) {
    if (!path || !*path) {
        return 0;
    }

    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string resolvedPath = AppPaths::resolveString(path);
    unsigned char* data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return texture;
}

