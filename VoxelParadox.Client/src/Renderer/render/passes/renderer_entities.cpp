// renderer_entities.cpp
// Enemy rendering slice: uploads shared FBX meshes and draws world enemies.

#include "render/core/renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "enemies/enemy_definition.hpp"
#include "enemies/enemy_runtime_helpers.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "render/core/renderer_internal.hpp"
#include "world/block/block.hpp"

namespace {

constexpr const char* ENTITY_VERT = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uVP;
uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uVP * worldPos;
}
)";

constexpr const char* ENTITY_FRAG = R"(
#version 460 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D uTexture;
uniform vec3 uCameraPos;
uniform vec4 uFogColor;
uniform float uFogDensity;
uniform float uAlpha;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uTexture, vUV);
    if (texel.a < 0.1) {
        discard;
    }

    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 lightDir = normalize(vec3(0.35, 1.0, 0.25));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);

    vec3 color = texel.rgb * (0.70 + diffuse * 0.30);
    color += texel.rgb * rim * 0.04;

    float dist = length(vWorldPos - uCameraPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    color = mix(color, uFogColor.rgb, fog);

    FragColor = vec4(color, texel.a * uAlpha);
}
)";

}  // namespace

bool Renderer::setupEntityModel(LoadedEntityModel& model,
                                const EntityModelAsset& asset) {
    for (LoadedEntityModel::Part& part : model.parts) {
        RendererInternal::deleteVertexArrayAndBuffer(part.vao, part.vbo);
    }
    model.parts.clear();
    if (model.texture != 0) {
        glDeleteTextures(1, &model.texture);
        model.texture = 0;
    }

    const GLuint texture = loadTexture2DFromFile(asset.texturePath.c_str(), true);
    if (texture == 0) {
        std::printf("[Entity] Failed to load texture: %s\n", asset.texturePath.c_str());
        return false;
    }

    model.parts.reserve(asset.parts.size());
    for (const EntityModelPartAsset& assetPart : asset.parts) {
        if (!assetPart.valid()) {
            continue;
        }

        LoadedEntityModel::Part gpuPart{};
        gpuPart.asset = assetPart;

        glGenVertexArrays(1, &gpuPart.vao);
        glGenBuffers(1, &gpuPart.vbo);
        glBindVertexArray(gpuPart.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpuPart.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(assetPart.vertices.size() *
                                             sizeof(EntityModelVertex)),
                     assetPart.vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EntityModelVertex),
                              reinterpret_cast<void*>(offsetof(EntityModelVertex, position)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EntityModelVertex),
                              reinterpret_cast<void*>(offsetof(EntityModelVertex, normal)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(EntityModelVertex),
                              reinterpret_cast<void*>(offsetof(EntityModelVertex, uv)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        model.parts.push_back(std::move(gpuPart));
    }

    model.asset = asset;
    model.texture = texture;
    return !model.parts.empty();
}

bool Renderer::setupObjBlockModel(LoadedObjBlockModel& model,
                                  const ObjBlockModelAsset& asset) {
    for (LoadedObjBlockModel::Part& part : model.parts) {
        RendererInternal::deleteVertexArrayAndBuffer(part.vao, part.vbo);
    }
    model.parts.clear();
    if (model.texture != 0) {
        glDeleteTextures(1, &model.texture);
        model.texture = 0;
    }

    const GLuint texture = loadTexture2DFromFile(asset.texturePath.c_str(), true);
    if (texture == 0) {
        std::printf("[BlockModel] Failed to load texture: %s\n", asset.texturePath.c_str());
        return false;
    }

    model.parts.reserve(asset.parts.size());
    for (const ObjBlockModelPartAsset& assetPart : asset.parts) {
        if (!assetPart.valid()) {
            continue;
        }

        LoadedObjBlockModel::Part gpuPart{};
        gpuPart.asset = assetPart;

        glGenVertexArrays(1, &gpuPart.vao);
        glGenBuffers(1, &gpuPart.vbo);
        glBindVertexArray(gpuPart.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpuPart.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(assetPart.vertices.size() *
                                             sizeof(ObjBlockModelVertex)),
                     assetPart.vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjBlockModelVertex),
                              reinterpret_cast<void*>(offsetof(ObjBlockModelVertex, position)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjBlockModelVertex),
                              reinterpret_cast<void*>(offsetof(ObjBlockModelVertex, normal)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjBlockModelVertex),
                              reinterpret_cast<void*>(offsetof(ObjBlockModelVertex, uv)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        model.parts.push_back(std::move(gpuPart));
    }

    model.asset = asset;
    model.texture = texture;
    return !model.parts.empty();
}

bool Renderer::setupEntityAssets() {
    if (!entityShader.compile(ENTITY_VERT, ENTITY_FRAG)) {
        std::printf("[Entity] Failed to compile entity shader.\n");
        return false;
    }

    std::string definitionError;
    const EnemyDefinition* guyDefinition =
        getEnemyDefinition(EnemyType::Guy, &definitionError);
    if (!guyDefinition) {
        std::printf("[Enemy] %s\n", definitionError.c_str());
        return false;
    }

    if (!setupEntityModel(guyModel_, guyDefinition->modelAsset)) {
        return false;
    }

    return true;
}

bool Renderer::setupBlockModelAssets() {
    customBlockModels_.clear();

    const auto& definitions = BlockRegistry::instance().definitions();
    for (const BlockDefinition& definition : definitions) {
        if (definition.customModelAssetPath.empty()) {
            continue;
        }

        std::string modelError;
        ObjBlockModelAsset modelAsset{};
        if (!loadObjBlockModel(definition.customModelAssetPath, modelAsset, modelError)) {
            std::printf("[BlockModel] %s\n", modelError.c_str());
            return false;
        }

        LoadedObjBlockModel loadedModel{};
        if (!setupObjBlockModel(loadedModel, modelAsset)) {
            return false;
        }

        customBlockModels_[static_cast<int>(definition.idValue)] = std::move(loadedModel);
    }

    return true;
}

void Renderer::cleanupEntityAssets() {
    for (LoadedEntityModel::Part& part : guyModel_.parts) {
        RendererInternal::deleteVertexArrayAndBuffer(part.vao, part.vbo);
    }
    guyModel_.parts.clear();
    if (guyModel_.texture != 0) {
        glDeleteTextures(1, &guyModel_.texture);
        guyModel_.texture = 0;
    }
    guyModel_.asset = EntityModelAsset{};
}

void Renderer::cleanupBlockModelAssets() {
    for (auto& [typeKey, model] : customBlockModels_) {
        (void)typeKey;
        for (LoadedObjBlockModel::Part& part : model.parts) {
            RendererInternal::deleteVertexArrayAndBuffer(part.vao, part.vbo);
        }
        model.parts.clear();
        if (model.texture != 0) {
            glDeleteTextures(1, &model.texture);
            model.texture = 0;
        }
        model.asset = ObjBlockModelAsset{};
    }

    customBlockModels_.clear();
}

const Renderer::LoadedObjBlockModel* Renderer::getLoadedCustomBlockModel(BlockId type) const {
    const auto found = customBlockModels_.find(static_cast<int>(type));
    if (found == customBlockModels_.end()) {
        return nullptr;
    }

    return found->second.ready() ? &found->second : nullptr;
}

float Renderer::getCustomBlockModelFitScale(BlockId type) const {
    const LoadedObjBlockModel* model = getLoadedCustomBlockModel(type);
    return model ? model->asset.fitScale : 1.0f;
}

glm::mat4 Renderer::buildWorldBlockModelTransform(const LoadedObjBlockModel& model,
                                                  const glm::vec3& cellOrigin,
                                                  float yRotationRadians) const {
    const glm::vec3 pivot(0.5f, 0.0f, 0.5f);
    glm::mat4 rotation(1.0f);
    rotation = glm::rotate(rotation, yRotationRadians, glm::vec3(0.0f, 1.0f, 0.0f));

    return glm::translate(glm::mat4(1.0f), cellOrigin + pivot) *
           rotation *
           glm::translate(glm::mat4(1.0f), model.asset.blockOffset - pivot);
}

glm::mat4 Renderer::buildCenteredBlockModelTransform(const LoadedObjBlockModel& model,
                                                     const glm::vec3& cellCenter,
                                                     const glm::mat4& rotationScale) const {
    return glm::translate(glm::mat4(1.0f), cellCenter) *
           rotationScale *
           glm::translate(glm::mat4(1.0f), model.asset.centeredOffset);
}

void Renderer::drawLoadedObjBlockModel(const LoadedObjBlockModel& model,
                                       const glm::mat4& modelMatrix) {
    if (!model.ready()) {
        return;
    }

    entityShader.setMat4("uModel", modelMatrix);
    glBindTexture(GL_TEXTURE_2D, model.texture);

    for (const LoadedObjBlockModel::Part& part : model.parts) {
        if (!part.ready()) {
            continue;
        }

        glBindVertexArray(part.vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(part.asset.vertices.size()));
    }
}

namespace {

void appendWireBoxVertices(const glm::mat4& transform, const glm::vec3& halfExtents,
                           std::array<float, 72>& outVertices) {
    const std::array<glm::vec3, 8> localCorners = {{
        {-halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x,  halfExtents.y,  halfExtents.z},
        {-halfExtents.x,  halfExtents.y,  halfExtents.z},
    }};

    static constexpr int kEdgeIndices[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    std::size_t vertexIndex = 0;
    for (const auto& edge : kEdgeIndices) {
        const glm::vec3 start =
            glm::vec3(transform * glm::vec4(localCorners[edge[0]], 1.0f));
        const glm::vec3 end =
            glm::vec3(transform * glm::vec4(localCorners[edge[1]], 1.0f));
        outVertices[vertexIndex++] = start.x;
        outVertices[vertexIndex++] = start.y;
        outVertices[vertexIndex++] = start.z;
        outVertices[vertexIndex++] = end.x;
        outVertices[vertexIndex++] = end.y;
        outVertices[vertexIndex++] = end.z;
    }
}

}  // namespace

void Renderer::renderTargetSelectionWireframe(const FractalWorld& world,
                                              const Player& player,
                                              const glm::mat4& vp) {
    if (!player.hasTargetBlock()) {
        return;
    }

    const BlockId targetType = world.getBlock(player.getTargetBlock());
    if (!canTargetBlock(targetType)) {
        return;
    }

    glm::vec3 selectionMin(0.0f);
    glm::vec3 selectionMax(1.0f);
    if (!getBlockSelectionBounds(targetType, selectionMin, selectionMax)) {
        return;
    }

    const glm::vec3 inflate(0.015f);
    const glm::vec3 wireMin = selectionMin - inflate;
    const glm::vec3 wireMax = selectionMax + inflate;
    const glm::vec3 center = glm::vec3(player.getTargetBlock()) + (wireMin + wireMax) * 0.5f;
    const glm::vec3 halfExtents = (wireMax - wireMin) * 0.5f;

    std::array<float, 72> vertices{};
    appendWireBoxVertices(glm::translate(glm::mat4(1.0f), center), halfExtents, vertices);

    lineShader.use();
    lineShader.setMat4("uMVP", vp);
    lineShader.setVec4("uColor", glm::vec4(0.0f, 0.0f, 0.0f, 0.95f));

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glLineWidth(1.0f);

    glBindVertexArray(portalFrameVAO);
    glBindBuffer(GL_ARRAY_BUFFER, portalFrameVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void Renderer::renderEntities(const FractalWorld& world, const glm::mat4& vp,
                              const glm::vec3& cameraPos, const glm::vec4& fog,
                              int depth, int renderDistance, float time, float alpha) {
    (void)time;

    if (world.enemies.empty()) {
        return;
    }

    entityShader.use();
    entityShader.setMat4("uVP", vp);
    entityShader.setVec3("uCameraPos", cameraPos);
    entityShader.setVec4("uFogColor", fog);
    entityShader.setFloat("uFogDensity", computeFogDensity(depth, renderDistance));
    entityShader.setFloat("uAlpha", alpha);
    entityShader.setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    if (alpha < 0.999f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    const float maxRenderDistance = std::max(24.0f, static_cast<float>(renderDistance) * 18.0f);

    for (const WorldEnemy& entity : world.enemies) {
        const LoadedEntityModel* model = nullptr;
        switch (entity.type) {
            case EnemyType::Guy:
                model = guyModel_.ready() ? &guyModel_ : nullptr;
                break;
        }

        if (!model) {
            continue;
        }
        if (glm::length(entity.position - cameraPos) > maxRenderDistance) {
            continue;
        }

        std::string definitionError;
        const EnemyDefinition* definition =
            getEnemyDefinition(entity.type, &definitionError);
        if (!definition) {
            std::printf("[Enemy] %s\n", definitionError.c_str());
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, model->texture);

        for (std::size_t partIndex = 0; partIndex < model->parts.size(); ++partIndex) {
            const LoadedEntityModel::Part& part = model->parts[partIndex];
            if (!part.ready()) {
                continue;
            }

            glm::mat4 partModelMatrix =
                EnemyRuntime::buildEnemyPartWorldMatrix(entity, *definition, part.asset);

            entityShader.setMat4("uModel", partModelMatrix);
            glBindVertexArray(part.vao);
            glDrawArrays(GL_TRIANGLES, 0,
                         static_cast<GLsizei>(part.asset.vertices.size()));
        }
    }

    if (EnemyDebugView::headTriggerWireframeEnabled) {
        lineShader.use();
        lineShader.setMat4("uMVP", vp);
        lineShader.setVec4("uColor", glm::vec4(1.0f, 0.92f, 0.18f, 0.95f));
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glLineWidth(1.75f);
        glBindVertexArray(portalFrameVAO);
        glBindBuffer(GL_ARRAY_BUFFER, portalFrameVBO);

        for (const WorldEnemy& entity : world.enemies) {
            std::string definitionError;
            const EnemyDefinition* definition =
                getEnemyDefinition(entity.type, &definitionError);
            if (!definition) {
                continue;
            }

            const EnemyChargePlayerOnLookTriggerModule* chargeModule =
                EnemyRuntime::findChargePlayerOnLookTriggerModule(entity);
            if (!chargeModule) {
                continue;
            }

            glm::mat4 triggerTransform(1.0f);
            if (!EnemyRuntime::tryBuildLookTriggerWorldTransform(
                    entity, *definition, *chargeModule, triggerTransform)) {
                continue;
            }

            const glm::vec3 scaledTriggerHalfExtents =
                chargeModule->triggerHalfExtents *
                glm::max(chargeModule->triggerSizeMultiplier, 0.01f);
            std::array<float, 72> vertices{};
            appendWireBoxVertices(triggerTransform, scaledTriggerHalfExtents,
                                  vertices);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
            glDrawArrays(GL_LINES, 0, 24);
        }

        glLineWidth(1.0f);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void Renderer::renderWorldBlockModels(const FractalWorld& world, const glm::mat4& vp,
                                      const glm::vec3& cameraPos, const glm::vec4& fog,
                                      int depth, int renderDistance, float time, float alpha) {
    (void)time;

    const float maxRenderDistance = std::max(24.0f, static_cast<float>(renderDistance) * 18.0f);

    entityShader.use();
    entityShader.setMat4("uVP", vp);
    entityShader.setVec3("uCameraPos", cameraPos);
    entityShader.setVec4("uFogColor", fog);
    entityShader.setFloat("uFogDensity", computeFogDensity(depth, renderDistance));
    entityShader.setFloat("uAlpha", alpha);
    entityShader.setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    for (const auto& [chunkCoord, chunkPtr] : world.chunks) {
        const Chunk* chunk = chunkPtr.get();
        if (!chunk || !chunk->generated || chunk->customModelBlocks().empty()) {
            continue;
        }

        const glm::vec3 chunkCenter =
            glm::vec3(chunkCoord * Chunk::SIZE + glm::ivec3(Chunk::SIZE / 2));
        if (glm::length(chunkCenter - cameraPos) > maxRenderDistance + Chunk::SIZE * 1.75f) {
            continue;
        }

        for (const Chunk::CustomModelBlockInstance& instance : chunk->customModelBlocks()) {
            const LoadedObjBlockModel* model = getLoadedCustomBlockModel(instance.type);
            if (!model) {
                continue;
            }

            const glm::ivec3 worldBlockPos = chunkCoord * Chunk::SIZE + instance.localPos;
            const glm::vec3 worldCellCenter = glm::vec3(worldBlockPos) + glm::vec3(0.5f);
            if (glm::length(worldCellCenter - cameraPos) > maxRenderDistance) {
                continue;
            }

            const float yawRadians =
                hash01(hash4i(worldBlockPos.x, worldBlockPos.y, worldBlockPos.z,
                              static_cast<int>(world.seed))) * 6.2831853f;
            drawLoadedObjBlockModel(
                *model,
                buildWorldBlockModelTransform(*model, glm::vec3(worldBlockPos), yawRadians));
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
