#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ObjBlockModelVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
};

struct ObjBlockModelPartAsset {
    std::string name{};
    std::vector<ObjBlockModelVertex> vertices{};
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool valid() const {
        return !vertices.empty();
    }
};

struct ObjBlockModelAsset {
    std::vector<ObjBlockModelPartAsset> parts{};
    std::string texturePath{};
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 blockOffset{0.0f};
    glm::vec3 centeredOffset{0.0f};
    float fitScale = 1.0f;

    bool valid() const {
        return !parts.empty() && !texturePath.empty();
    }
};

bool loadObjBlockModel(const std::filesystem::path& modelAssetPath,
                       ObjBlockModelAsset& outModel,
                       std::string& outError);
