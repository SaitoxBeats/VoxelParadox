#pragma once

// 1. Standard Library
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// 2. Third-party Libraries
#include <glad/glad.h>

// 3. Local Project Modules
#include "engine/shader.hpp"
#include "path/app_paths.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "world/block/block_registry.hpp"

namespace BlockTextureCache {

inline GLuint createSolidTexture(const unsigned char rgba[4]) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        1,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

inline GLuint loadBlockTexture(
    const std::filesystem::path& resolvedPath
) {
    if (resolvedPath.empty()) {
        return 0;
    }

    const GLuint texture =
        loadTexture2DFromFile(resolvedPath.string().c_str(), false);
    if (texture != 0) {
        return texture;
    }

    std::printf("[Blocks] Failed to load block texture: %s\n",
                resolvedPath.string().c_str());

    static constexpr unsigned char kFallbackWhitePixel[4] = {
        255, 255, 255, 255
    };
    return createSolidTexture(kFallbackWhitePixel);
}

inline std::vector<GLuint> loadBlockTextures(
    const std::vector<BlockDefinition>& definitions,
    std::vector<std::filesystem::path>* outTexturePaths = nullptr,
    std::vector<std::filesystem::file_time_type>* outWriteTimes = nullptr
) {
    std::vector<GLuint> textures;
    textures.reserve(definitions.size());

    if (outTexturePaths != nullptr) {
        outTexturePaths->clear();
    }

    if (outWriteTimes != nullptr) {
        outWriteTimes->clear();
    }

    for (const BlockDefinition& definition : definitions) {
        if (definition.textureAssetPath.empty()) {
            continue;
        }

        const std::filesystem::path resolvedPath =
            AppPaths::resolve(definition.textureAssetPath);
        std::error_code ec;
        const bool exists = std::filesystem::exists(resolvedPath, ec);
        const std::filesystem::file_time_type writeTime =
            (!ec && exists) ? std::filesystem::last_write_time(resolvedPath, ec)
                            : std::filesystem::file_time_type{};

        if (outTexturePaths != nullptr) {
            outTexturePaths->push_back(resolvedPath);
        }

        if (outWriteTimes != nullptr) {
            outWriteTimes->push_back(writeTime);
        }

        textures.push_back(loadBlockTexture(resolvedPath));
    }

    return textures;
}

inline void destroyBlockTextures(std::vector<GLuint>& textures) {
    for (const GLuint texture : textures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }

    textures.clear();
}

inline void bindBlockTextures(const std::vector<GLuint>& textures) {
    for (std::size_t index = 0; index < textures.size(); ++index) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
        glBindTexture(GL_TEXTURE_2D, textures[index]);
    }

    glActiveTexture(GL_TEXTURE0);
}

inline void configureBlockTextureSamplerBindings(
    Shader& shader,
    const std::vector<BlockDefinition>& definitions
) {
    if (shader.program == 0) {
        return;
    }

    shader.use();

    int textureUnit = 0;
    for (const BlockDefinition& definition : definitions) {
        if (definition.textureAssetPath.empty()) {
            continue;
        }

        const std::string samplerName = "uBlockTexture_" + definition.id;
        shader.setInt(samplerName.c_str(), textureUnit);
        ++textureUnit;
    }
}

} // namespace BlockTextureCache
