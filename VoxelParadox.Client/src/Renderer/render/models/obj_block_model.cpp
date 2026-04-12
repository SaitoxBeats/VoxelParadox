#include "render/models/obj_block_model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include "path/app_paths.hpp"

namespace {

glm::vec3 toGlm(const aiVector3D& value) {
    return glm::vec3(value.x, value.y, value.z);
}

glm::mat4 toGlm(const aiMatrix4x4& value) {
    return glm::mat4(
        value.a1, value.b1, value.c1, value.d1,
        value.a2, value.b2, value.c2, value.d2,
        value.a3, value.b3, value.c3, value.d3,
        value.a4, value.b4, value.c4, value.d4);
}

std::string trimPartName(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char ch) { return std::iscntrl(ch) != 0; }),
                value.end());
    return value;
}

std::string pickPartName(const aiNode* node, const aiMesh& mesh, unsigned int meshIndex) {
    if (node) {
        const std::string nodeName = trimPartName(node->mName.C_Str());
        if (!nodeName.empty()) {
            return nodeName;
        }
    }

    const std::string meshName = trimPartName(mesh.mName.C_Str());
    if (!meshName.empty()) {
        return meshName;
    }

    return "part_" + std::to_string(meshIndex);
}

std::string makeResourceRelativePath(const std::filesystem::path& absolutePath) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(absolutePath, AppPaths::resourcesRoot(), ec);
    if (!ec && !relative.empty()) {
        return relative.generic_string();
    }
    return absolutePath.generic_string();
}

std::string resolveTextureCandidate(const std::filesystem::path& modelAbsolutePath,
                                    const std::filesystem::path& candidatePath) {
    if (candidatePath.empty()) {
        return {};
    }

    const std::filesystem::path resolvedPath =
        candidatePath.is_absolute()
            ? candidatePath
            : (modelAbsolutePath.parent_path() / candidatePath).lexically_normal();
    if (std::filesystem::exists(resolvedPath)) {
        return makeResourceRelativePath(resolvedPath);
    }

    return {};
}

std::string findTextureNextToModel(const std::filesystem::path& modelAbsolutePath) {
    static constexpr std::array<const char*, 5> kExtensions = {
        ".png", ".tga", ".jpg", ".jpeg", ".bmp",
    };

    for (const char* extension : kExtensions) {
        const std::filesystem::path candidate =
            modelAbsolutePath.parent_path() / (modelAbsolutePath.stem().string() + extension);
        if (std::filesystem::exists(candidate)) {
            return makeResourceRelativePath(candidate);
        }
    }

    return {};
}

std::string resolveTexturePath(const std::filesystem::path& modelAbsolutePath,
                               const aiScene* scene) {
    if (!scene || !scene->HasMaterials()) {
        return findTextureNextToModel(modelAbsolutePath);
    }

    for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        const aiMaterial* material = scene->mMaterials[materialIndex];
        if (!material) {
            continue;
        }

        static constexpr aiTextureType kTextureTypes[] = {
            aiTextureType_BASE_COLOR,
            aiTextureType_DIFFUSE,
            aiTextureType_UNKNOWN,
        };

        for (aiTextureType textureType : kTextureTypes) {
            aiString texturePath;
            if (material->GetTexture(textureType, 0, &texturePath) != AI_SUCCESS) {
                continue;
            }

            const std::string resolved = resolveTextureCandidate(
                modelAbsolutePath, std::filesystem::path(texturePath.C_Str()));
            if (!resolved.empty()) {
                return resolved;
            }
        }
    }

    return findTextureNextToModel(modelAbsolutePath);
}

void appendSceneMeshVertices(const aiMesh& mesh, const glm::mat4& nodeTransform,
                             ObjBlockModelPartAsset& outPart,
                             glm::vec3& modelBoundsMin, glm::vec3& modelBoundsMax) {
    if (!mesh.HasPositions() || !mesh.HasFaces()) {
        return;
    }

    const glm::mat3 normalTransform =
        glm::transpose(glm::inverse(glm::mat3(nodeTransform)));

    outPart.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    outPart.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
        const aiFace& face = mesh.mFaces[faceIndex];
        if (face.mNumIndices != 3) {
            continue;
        }

        for (unsigned int localIndex = 0; localIndex < 3; ++localIndex) {
            const unsigned int vertexIndex = face.mIndices[localIndex];
            if (vertexIndex >= mesh.mNumVertices) {
                continue;
            }

            const glm::vec3 position =
                glm::vec3(nodeTransform * glm::vec4(toGlm(mesh.mVertices[vertexIndex]), 1.0f));
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (mesh.HasNormals()) {
                normal = glm::normalize(normalTransform * toGlm(mesh.mNormals[vertexIndex]));
            }
            const glm::vec2 uv = mesh.HasTextureCoords(0)
                                     ? glm::vec2(mesh.mTextureCoords[0][vertexIndex].x,
                                                 mesh.mTextureCoords[0][vertexIndex].y)
                                     : glm::vec2(0.0f);

            outPart.vertices.push_back(ObjBlockModelVertex{position, normal, uv});
            outPart.boundsMin = glm::min(outPart.boundsMin, position);
            outPart.boundsMax = glm::max(outPart.boundsMax, position);
            modelBoundsMin = glm::min(modelBoundsMin, position);
            modelBoundsMax = glm::max(modelBoundsMax, position);
        }
    }
}

void appendNodeMeshesRecursive(const aiScene& scene, const aiNode* node,
                               const glm::mat4& parentTransform,
                               ObjBlockModelAsset& outModel) {
    if (!node) {
        return;
    }

    const glm::mat4 nodeTransform = parentTransform * toGlm(node->mTransformation);

    for (unsigned int localMeshIndex = 0; localMeshIndex < node->mNumMeshes; ++localMeshIndex) {
        const unsigned int sceneMeshIndex = node->mMeshes[localMeshIndex];
        if (sceneMeshIndex >= scene.mNumMeshes) {
            continue;
        }

        const aiMesh* mesh = scene.mMeshes[sceneMeshIndex];
        if (!mesh) {
            continue;
        }

        ObjBlockModelPartAsset part{};
        part.name = pickPartName(node, *mesh, sceneMeshIndex);
        appendSceneMeshVertices(*mesh, nodeTransform, part,
                                outModel.boundsMin, outModel.boundsMax);
        if (part.valid()) {
            outModel.parts.push_back(std::move(part));
        }
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        appendNodeMeshesRecursive(scene, node->mChildren[childIndex], nodeTransform, outModel);
    }
}

}  // namespace

bool loadObjBlockModel(const std::filesystem::path& modelAssetPath,
                       ObjBlockModelAsset& outModel,
                       std::string& outError) {
    outModel = ObjBlockModelAsset{};

    const std::filesystem::path modelAbsolutePath = AppPaths::resolve(modelAssetPath);
    if (!std::filesystem::exists(modelAbsolutePath)) {
        outError = "Failed to open OBJ model: " + modelAbsolutePath.string();
        return false;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        modelAbsolutePath.string(),
        aiProcess_Triangulate |
            aiProcess_SortByPType |
            aiProcess_JoinIdenticalVertices |
            aiProcess_GenSmoothNormals |
            aiProcess_ValidateDataStructure);

    if (!scene || !scene->HasMeshes()) {
        outError = "Failed to parse OBJ model: " + modelAbsolutePath.string() +
                   " (" + importer.GetErrorString() + ")";
        return false;
    }

    outModel.texturePath = resolveTexturePath(modelAbsolutePath, scene);
    if (outModel.texturePath.empty()) {
        outError = "Failed to resolve OBJ texture next to model: " +
                   modelAbsolutePath.string();
        return false;
    }

    outModel.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    outModel.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

    appendNodeMeshesRecursive(*scene, scene->mRootNode, glm::mat4(1.0f), outModel);

    if (outModel.parts.empty()) {
        outError = "OBJ import produced no triangles: " + modelAbsolutePath.string();
        return false;
    }

    const glm::vec3 boundsSize = outModel.boundsMax - outModel.boundsMin;
    const float maxDimension =
        std::max(boundsSize.x, std::max(boundsSize.y, boundsSize.z));
    if (maxDimension > 0.0001f) {
        outModel.fitScale = 1.0f / maxDimension;
    }

    outModel.blockOffset = glm::vec3(
        0.5f - ((outModel.boundsMin.x + outModel.boundsMax.x) * 0.5f),
        -outModel.boundsMin.y,
        0.5f - ((outModel.boundsMin.z + outModel.boundsMax.z) * 0.5f));
    outModel.centeredOffset = -((outModel.boundsMin + outModel.boundsMax) * 0.5f);

    return true;
}
