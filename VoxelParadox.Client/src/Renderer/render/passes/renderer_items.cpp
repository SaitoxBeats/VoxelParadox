// renderer_items.cpp
// Unity mental model: this is the "items and overlays" slice of the renderer.
// It draws HUD previews, the equipped item, volumetric dust, and dropped blocks.

// 1. Standard Library
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

// 3. Local Project Modules
#include "engine/camera.hpp"
#include "engine/engine.hpp"
#include "render/cache/item_texture_cache.hpp"
#include "render/config/dust_particle_config.hpp"
#include "render/config/hand_animation_config.hpp"
#include "render/config/hotbar_preview_config.hpp"
#include "render/core/renderer.hpp"
#include "render/core/renderer_internal.hpp"
#include "render/hud/hud.hpp"
#include "world/generation/chunk.hpp"
#include "world/generation/fractal_world.hpp"

namespace {

glm::mat4 buildDroppedSpriteModel(const glm::vec3& itemPosition, const glm::vec3& cameraPos,
                                  float time, float spinPhase) {
    const float bobOffset = std::sin(time * 2.4f + spinPhase) * 0.06f;
    const glm::vec3 spritePosition = itemPosition + glm::vec3(0.0f, bobOffset, 0.0f);

    glm::vec3 toCamera = cameraPos - spritePosition;
    if (glm::dot(toCamera, toCamera) < 1e-6f) {
        toCamera = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        toCamera = glm::normalize(toCamera);
    }

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(worldUp, toCamera);
    if (glm::dot(right, right) < 1e-6f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(right);
    }
    const glm::vec3 up = glm::normalize(glm::cross(toCamera, right));

    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(right, 0.0f);
    basis[1] = glm::vec4(up, 0.0f);
    basis[2] = glm::vec4(toCamera, 0.0f);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), spritePosition) * basis;
    model = glm::rotate(model, time * 1.7f + spinPhase, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(0.42f, 0.42f, 1.0f));
    return model;
}

glm::vec3 buildDroppedModelCenter(const glm::vec3& itemPosition, float time, float spinPhase) {
    return itemPosition + glm::vec3(0.0f, std::sin(time * 2.4f + spinPhase) * 0.06f, 0.0f);
}

float smooth01(float value) {
    const float clamped = glm::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

constexpr int kBreakingParticleStartCount = 6;
constexpr int kBreakingParticleProgressCount = 3;
constexpr int kBreakingParticleFinishCount = 34;
constexpr int kBreakingParticleMaxCount = 192;
constexpr std::size_t kBreakingParticleInitialVertexCapacity = 1024;
constexpr float kBreakingParticleEmissionInterval = 0.055f;
constexpr float kBreakingParticleProgressInterval = 0.12f;
constexpr float kBreakingParticleGravity = 5.8f;
constexpr float kBreakingParticleDrag = 0.985f;
constexpr float kBreakingParticleFaceOffset = 0.515f;
constexpr float kBreakingParticleFaceSpread = 0.82f;
constexpr float kBreakingParticleBaseLifetime = 0.46f;
constexpr float kBreakingParticleFinishLifetime = 0.74f;
constexpr float kBreakingParticleBaseSpeed = 1.65f;
constexpr float kBreakingParticleFinishSpeed = 3.15f;
constexpr float kBreakingParticleMinSize = 0.055f;
constexpr float kBreakingParticleMaxSize = 0.105f;
constexpr float kBreakingParticleFinishMinSize = 0.065f;
constexpr float kBreakingParticleFinishMaxSize = 0.145f;
constexpr float kTwoPi = 6.28318530718f;

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float lengthSq = glm::dot(value, value);
    if (lengthSq <= 1e-6f) {
        return fallback;
    }

    return value * (1.0f / std::sqrt(lengthSq));
}

glm::vec3 randomUnitDirection(float rx, float ry) {
    const float z = rx * 2.0f - 1.0f;
    const float radius = std::sqrt(glm::max(0.0f, 1.0f - z * z));
    const float angle = ry * kTwoPi;
    return glm::vec3(std::cos(angle) * radius, z, std::sin(angle) * radius);
}

glm::vec3 chooseFallbackFaceNormal(std::uint32_t hash) {
    switch (hash % 6u) {
    case 0u:
        return glm::vec3(-1.0f, 0.0f, 0.0f);
    case 1u:
        return glm::vec3(1.0f, 0.0f, 0.0f);
    case 2u:
        return glm::vec3(0.0f, -1.0f, 0.0f);
    case 3u:
        return glm::vec3(0.0f, 1.0f, 0.0f);
    case 4u:
        return glm::vec3(0.0f, 0.0f, -1.0f);
    default:
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
}

void buildParticleBasis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent) {
    const glm::vec3 helper =
        std::fabs(normal.y) < 0.92f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                    : glm::vec3(1.0f, 0.0f, 0.0f);
    tangent = safeNormalize(glm::cross(helper, normal), glm::vec3(1.0f, 0.0f, 0.0f));
    bitangent = safeNormalize(glm::cross(normal, tangent), glm::vec3(0.0f, 0.0f, 1.0f));
}

void appendBlockBreakParticleQuad(
    std::vector<Renderer::BlockBreakParticleVertex>& vertices,
    const Renderer::BlockBreakParticle& particle,
    int depth
) {
    const float lifetimeT =
        glm::clamp(particle.age / glm::max(particle.lifetime, 0.001f), 0.0f, 1.0f);
    const float scaleFade = 1.0f - smooth01(glm::clamp((lifetimeT - 0.62f) / 0.38f, 0.0f, 1.0f));
    const float halfSize = particle.size * glm::max(0.08f, scaleFade) * 0.5f;

    const float sinRotation = std::sin(particle.rotation);
    const float cosRotation = std::cos(particle.rotation);
    const glm::vec3 right =
        (particle.tangent * cosRotation + particle.bitangent * sinRotation) * halfSize;
    const glm::vec3 up =
        (-particle.tangent * sinRotation + particle.bitangent * cosRotation) * halfSize;
    const glm::vec4 color = getBlockColor(particle.blockType, depth, 0);
    const float material = getBlockMaterialId(particle.blockType);

    const glm::vec3 corners[4] = {
        particle.position - right - up,
        particle.position + right - up,
        particle.position + right + up,
        particle.position - right + up,
    };

    const int indices[6] = {0, 1, 2, 0, 2, 3};
    for (const int index : indices) {
        vertices.push_back({
            corners[index],
            particle.normal,
            color,
            1.0f,
            material,
        });
    }
}

}  // namespace

void Renderer::renderHUD3DOverlays(const Player& player, const FractalWorld* world, int depth,
                                   float time) {
    renderHotbarPreviews(player, world, depth, time);
}

void Renderer::renderItemPreviewInRect(const glm::ivec4& slotRectTopLeft, int screenHeight,
                                       int inset, const InventoryItem& item, int depth,
                                       float time, const HUDItemPreviewConfig& config,
                                       bool spinning, float scaleMultiplier,
                                       const FractalWorld* world) {
    if (item.empty()) {
        return;
    }

    const glm::ivec4 previewRect = buildPreviewRect(slotRectTopLeft, screenHeight, inset);
    HUD3DPreviewRequest request{};
    request.camera = config.camera;
    request.style = config.style;

    glm::vec3 spinAxis = config.spinAxis;
    if (glm::dot(spinAxis, spinAxis) < 1e-6f) {
        spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        spinAxis = glm::normalize(spinAxis);
    }

    if (usesItemTexturePreview(item)) {
        RendererInternal::renderHUD3DPreviewInRect(
            previewRect, request,
            [&](const glm::mat4& previewVP, const glm::vec3&, const HUD3DPreviewStyle&) {
                renderItemSprite(item.itemId,
                                 previewVP,
                                 buildPreviewItemModel(config, time, spinning, scaleMultiplier),
                                 1.0f, false);
            });
        return;
    }

    if (item.isBlock() && usesCustomBlockModel(item.blockType)) {
        const LoadedObjBlockModel* blockModel = getLoadedCustomBlockModel(item.blockType);
        if (!blockModel) {
            return;
        }

        RendererInternal::renderHUD3DPreviewInRect(
            previewRect, request,
            [&](const glm::mat4& previewVP, const glm::vec3& previewCameraPos,
                const HUD3DPreviewStyle& style) {
                glm::mat4 rotationScale(1.0f);
                rotationScale = glm::rotate(rotationScale,
                                            glm::radians(config.staticRotationDegrees.x),
                                            glm::vec3(1.0f, 0.0f, 0.0f));
                rotationScale = glm::rotate(rotationScale,
                                            glm::radians(config.staticRotationDegrees.y),
                                            glm::vec3(0.0f, 1.0f, 0.0f));
                rotationScale = glm::rotate(rotationScale,
                                            glm::radians(config.staticRotationDegrees.z),
                                            glm::vec3(0.0f, 0.0f, 1.0f));
                if (spinning) {
                    rotationScale = glm::rotate(rotationScale, time * config.spinSpeed,
                                                spinAxis);
                }

                const float scale =
                    config.cubeScale * 2.2f * scaleMultiplier * blockModel->asset.fitScale;
                rotationScale = glm::scale(rotationScale, glm::vec3(scale));

                entityShader.use();
                entityShader.setMat4("uVP", previewVP);
                entityShader.setVec3("uCameraPos", previewCameraPos);
                entityShader.setVec4("uFogColor", style.backgroundColor);
                entityShader.setFloat("uFogDensity", 0.0f);
                entityShader.setFloat("uAlpha", 1.0f);
                entityShader.setInt("uTexture", 0);

                glActiveTexture(GL_TEXTURE0);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glDisable(GL_CULL_FACE);

                drawLoadedObjBlockModel(
                    *blockModel,
                    buildCenteredBlockModelTransform(*blockModel, config.cubeCenter,
                                                     rotationScale));

                glBindVertexArray(0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glEnable(GL_CULL_FACE);
                glDisable(GL_BLEND);
            });
        return;
    }

    if (!item.isBlock() || !isSolid(item.blockType)) {
        return;
    }

    RendererInternal::renderHUD3DPreviewInRect(
        previewRect, request,
        [&](const glm::mat4& previewVP, const glm::vec3& previewCameraPos,
            const HUD3DPreviewStyle& style) {
            glm::mat4 rotation(1.0f);
            rotation = glm::rotate(rotation, glm::radians(config.staticRotationDegrees.x),
                                   glm::vec3(1.0f, 0.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(config.staticRotationDegrees.y),
                                   glm::vec3(0.0f, 1.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(config.staticRotationDegrees.z),
                                   glm::vec3(0.0f, 0.0f, 1.0f));
            if (spinning) {
                rotation = glm::rotate(rotation, time * config.spinSpeed, spinAxis);
            }

            const glm::vec3 blockOrigin = glm::floor(config.cubeCenter);
            uploadBlockCube(item.blockType, blockOrigin, depth);
            const float scale = config.cubeScale * scaleMultiplier;
            const glm::mat4 model =
                glm::translate(glm::mat4(1.0f), config.cubeCenter) * rotation *
                glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
                glm::translate(glm::mat4(1.0f), -(blockOrigin + glm::vec3(0.5f)));

            blockShader.use();
            blockShader.setMat4("uVP", previewVP);
            blockShader.setMat4("uModel", model);
            blockShader.setVec3("uCameraPos", previewCameraPos);
            blockShader.setVec4("uFogColor", style.backgroundColor);
            blockShader.setFloat("uFogDensity", 0.0f);
            blockShader.setFloat("uTime", time);
            blockShader.setFloat("uAlpha", 1.0f);
            blockShader.setFloat("uAoStrength", 1.0f);
            blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(world, depth));
            blockShader.setInt("uUseLocalMaterialSpace", 1);
            blockShader.setInt("uPointLightCount", 0);
            bindBlockTextures();
            setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
            setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);

            glBindVertexArray(breakBlockVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
        });
}

void Renderer::renderDepthIndicator(int depth) {
    (void)depth;
}

glm::vec4 Renderer::getBiomeMaterialTint(const FractalWorld* world, int depth) const {
    std::string biomeKey = "default";
    if (world) {
        if (!world->biomeSelection.storageId.empty()) {
            biomeKey = world->biomeSelection.storageId;
        } else if (world->biomePreset && !world->biomePreset->id.empty()) {
            biomeKey = world->biomePreset->id;
        }
    }

    const std::uint32_t hash = RendererInternal::hashStringFNV1a(biomeKey) ^
                               (0x9E3779B9u * static_cast<std::uint32_t>(depth + 1));
    const float hue =
        std::fmod(hash01(hash ^ 0x68BC21EBu) + static_cast<float>(depth) * 0.07f, 1.0f);
    const float saturation = 0.20f + hash01(hash ^ 0x02E5BE93u) * 0.24f;
    const float value = glm::min(1.0f, 0.92f + hash01(hash ^ 0xA54FF53Au) * 0.10f);
    const glm::vec3 tintColor = hsvToRgb(hue, saturation, value);
    const glm::vec3 tint =
        glm::clamp(glm::mix(glm::vec3(1.0f), tintColor, 0.35f), glm::vec3(0.72f),
                   glm::vec3(1.28f));
    const float emission = 0.92f + hash01(hash ^ 0x510E527Fu) * 0.38f;
    return glm::vec4(tint, emission);
}

glm::ivec4 Renderer::buildPreviewRect(const glm::ivec4& slotRectTopLeft, int screenHeight,
                                      int inset) const {
    const int x = slotRectTopLeft.x + inset;
    const int width = glm::max(1, slotRectTopLeft.z - inset * 2);
    const int height = glm::max(1, slotRectTopLeft.w - inset * 2);
    const int yFromTop = slotRectTopLeft.y + inset;
    const int y = screenHeight - yFromTop - height;
    return glm::ivec4(x, y, width, height);
}

void Renderer::renderHotbarPreviews(const Player& player, const FractalWorld* world,
                                    int depth, float time) {
    const glm::vec2 viewportSize = ENGINE::GETVIEWPORTSIZE();
    const int screenWidth = static_cast<int>(viewportSize.x);
    const int screenHeight = static_cast<int>(viewportSize.y);
    if (screenWidth <= 0 || screenHeight <= 0) {
        return;
    }

    const auto& preview = HUDHotbarPreview::config;
    const ResolvedHotbarLayout hotbarLayout =
        resolveHotbarLayout(preview.layout, screenWidth, screenHeight);

    const PlayerHotbar& hotbar = player.getHotbar();
    for (int slotIndex = 0; slotIndex < PlayerHotbar::SLOT_COUNT; slotIndex++) {
        const PlayerHotbar::Slot& slot = hotbar.getSlot(slotIndex);
        if (slot.empty()) {
            continue;
        }

        const bool selected = slotIndex == player.getSelectedHotbarIndex();
        renderItemPreviewInRect(
            hotbarLayout.slotRects[static_cast<std::size_t>(slotIndex)], screenHeight,
            preview.layout.slotPreviewInset, slot.item, depth, time, preview.item, selected,
            selected ? preview.item.selectedScaleMultiplier : 1.0f, world);
    }
}

glm::mat4 Renderer::buildPreviewItemModel(const HUDItemPreviewConfig& config, float time,
                                          bool spinning, float scaleMultiplier) const {
    glm::vec3 spinAxis = config.spinAxis;
    if (glm::dot(spinAxis, spinAxis) < 1e-6f) {
        spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        spinAxis = glm::normalize(spinAxis);
    }

    glm::mat4 model(1.0f);
    model = glm::translate(model, config.cubeCenter);
    model = glm::rotate(model, glm::radians(config.staticRotationDegrees.x),
                        glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(config.staticRotationDegrees.y),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(config.staticRotationDegrees.z),
                        glm::vec3(0.0f, 0.0f, 1.0f));
    if (spinning) {
        model = glm::rotate(model, time * config.spinSpeed, spinAxis);
    }
    model = glm::scale(model, glm::vec3(config.cubeScale * 2.2f * scaleMultiplier,
                                        config.cubeScale * 2.2f * scaleMultiplier, 1.0f));
    return model;
}

void Renderer::setupBreakBlockCube() {
    glGenVertexArrays(1, &breakBlockVAO);
    glGenBuffers(1, &breakBlockVBO);
    glBindVertexArray(breakBlockVAO);
    glBindBuffer(GL_ARRAY_BUFFER, breakBlockVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Chunk::Vertex) * 36, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Chunk::Vertex),
                          reinterpret_cast<void*>(offsetof(Chunk::Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Chunk::Vertex),
                          reinterpret_cast<void*>(offsetof(Chunk::Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Chunk::Vertex),
                          reinterpret_cast<void*>(offsetof(Chunk::Vertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Chunk::Vertex),
                          reinterpret_cast<void*>(offsetof(Chunk::Vertex, ao)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Chunk::Vertex),
                          reinterpret_cast<void*>(offsetof(Chunk::Vertex, material)));
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);
}

void Renderer::setupItemSpriteQuad() {
    const float vertices[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.0f, 1.0f, 1.0f,
        0.5f,  0.5f,  0.0f, 1.0f, 0.0f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
        0.5f,  0.5f,  0.0f, 1.0f, 0.0f, -0.5f, 0.5f,  0.0f, 0.0f, 0.0f,
    };

    glGenVertexArrays(1, &itemSpriteVAO);
    glGenBuffers(1, &itemSpriteVBO);
    glBindVertexArray(itemSpriteVAO);
    glBindBuffer(GL_ARRAY_BUFFER, itemSpriteVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

GLuint Renderer::getItemTexture(ItemId itemId) {
    if (getItemTexturePath(itemId) == nullptr) {
        return 0;
    }

    const auto found = itemTextureCache.find(itemId);
    if (found != itemTextureCache.end()) {
        return found->second;
    }

    const char* path = getItemTexturePath(itemId);
    if (!path) {
        return 0;
    }

    const GLuint texture = loadTexture2DFromFile(path, false);
    if (texture == 0) {
        return 0;
    }

    itemTextureCache[itemId] = texture;
    return texture;
}

void Renderer::renderItemSprite(ItemId itemId, const glm::mat4& vp, const glm::mat4& model,
                                float alpha, bool depthTest) {
    const GLuint texture = getItemTexture(itemId);
    if (texture == 0) {
        return;
    }

    itemSpriteShader.use();
    itemSpriteShader.setMat4("uVP", vp);
    itemSpriteShader.setMat4("uModel", model);
    itemSpriteShader.setFloat("uAlpha", alpha);
    itemSpriteShader.setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(itemSpriteVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderHeldItem(const Player& player, const FractalWorld* world,
                              const glm::mat4& vp, int depth, float time,
                              float visibility) {
    const float dt = glm::max(ENGINE::GETDELTATIME(), 0.0f);
    HandAnimation::state.update(dt, HandAnimation::config);

    // Resolve which item to display.
    // During a place-hold, keep showing the previous item so it doesn't
    // vanish from the hand the instant the block is placed.
    static InventoryItem placeHoldSnapshot{};

    const InventoryItem& liveItem =
        heldItemTransition.swapAnimating ? heldItemTransition.currentItem
                                         : player.getSelectedHotbarItem();

    if (!liveItem.empty()) {
        placeHoldSnapshot = liveItem;
    }

    const InventoryItem& heldItem =
        (liveItem.empty() && HandAnimation::state.isPlaceHolding())
            ? placeHoldSnapshot
            : liveItem;

    if (heldItem.empty() || visibility <= 0.001f) {
        return;
    }

    const float verticalOffset =
        heldItemTransition.swapAnimating ? heldItemTransition.swapVerticalOffset : 0.0f;

    if (usesItemTexturePreview(heldItem)) {
        renderHeldSpriteItem(player, heldItem.itemId, vp, time, visibility, verticalOffset);
        return;
    }

    if (heldItem.isBlock() && usesCustomBlockModel(heldItem.blockType)) {
        renderHeldCustomBlockModel(player, world, heldItem.blockType, vp, depth, time,
                                   visibility, verticalOffset);
        return;
    }

    if (isPlaceableInventoryItem(heldItem)) {
        renderHeldBlock(player, world, heldItem.blockType, vp, depth, time, visibility,
                        verticalOffset);
    }
}

void Renderer::renderHeldSpriteItem(const Player& player, ItemId heldItemId,
                                    const glm::mat4& vp, float time, float visibility,
                                    float verticalOffset) {
    const glm::vec3 forward = glm::normalize(player.camera.getForward());
    const glm::vec3 right = glm::normalize(player.camera.getRight());
    const glm::vec3 up = glm::normalize(player.camera.getUp());
    const auto& heldItemConfig = HeldItemDebugView::config;
    const auto& handAnim = HandAnimation::state;
    const auto& handConfig = HandAnimation::config;
    const float shownAmount = visibility * visibility * (3.0f - 2.0f * visibility);
    const float idleLift = std::sin(time * handConfig.idleSwaySpeed) * handConfig.idleSwayAmount;
    const float hiddenOffset = (1.0f - shownAmount) * 0.62f;

    const glm::vec3 animTranslation = handAnim.getTranslationOffset(handConfig);
    const glm::vec3 position =
        player.camera.position + forward * heldItemConfig.spritePositionOffset.x +
        right * heldItemConfig.spritePositionOffset.y +
        up * (heldItemConfig.spritePositionOffset.z - hiddenOffset + idleLift +
              verticalOffset) +
        forward * animTranslation.x +
        right * animTranslation.y +
        up * animTranslation.z;

    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(right, 0.0f);
    basis[1] = glm::vec4(up, 0.0f);
    basis[2] = glm::vec4(-forward, 0.0f);

    const glm::vec3 animRotation = handAnim.getRotationOffsetDegrees(handConfig);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position) * basis;
    model = glm::rotate(model, glm::radians(heldItemConfig.spriteRotationDegrees.x + animRotation.x),
                        glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(heldItemConfig.spriteRotationDegrees.y + animRotation.y),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(heldItemConfig.spriteRotationDegrees.z + animRotation.z),
                        glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(30.0f) * (1.0f - shownAmount),
                        glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model,
                       glm::vec3(heldItemConfig.spriteScale, heldItemConfig.spriteScale, 1.0f));

    renderItemSprite(heldItemId, vp, model, shownAmount, false);
}

void Renderer::renderHeldCustomBlockModel(const Player& player, const FractalWorld* world,
                                          BlockId heldType, const glm::mat4& vp, int depth,
                                          float time, float visibility,
                                          float verticalOffset) {
    (void)world;
    const LoadedObjBlockModel* blockModel = getLoadedCustomBlockModel(heldType);
    if (!blockModel) {
        return;
    }

    const glm::vec3 forward = glm::normalize(player.camera.getForward());
    const glm::vec3 right = glm::normalize(player.camera.getRight());
    const glm::vec3 up = glm::normalize(player.camera.getUp());
    const auto& heldItemConfig = HeldItemDebugView::config;
    const auto& handAnim = HandAnimation::state;
    const auto& handConfig = HandAnimation::config;
    const float shownAmount = visibility * visibility * (3.0f - 2.0f * visibility);
    const float idleLift = std::sin(time * handConfig.idleSwaySpeed) * handConfig.idleSwayAmount;
    const float hiddenOffset = (1.0f - shownAmount) * 0.72f;

    const glm::vec3 animTranslation = handAnim.getTranslationOffset(handConfig);
    const glm::vec3 position =
        player.camera.position + forward * heldItemConfig.blockPositionOffset.x +
        right * heldItemConfig.blockPositionOffset.y +
        up * (heldItemConfig.blockPositionOffset.z - hiddenOffset + idleLift +
              verticalOffset) +
        forward * animTranslation.x +
        right * animTranslation.y +
        up * animTranslation.z;

    const glm::vec3 animRotation = handAnim.getRotationOffsetDegrees(handConfig);
    glm::mat4 rotationScale(1.0f);
    rotationScale[0] = glm::vec4(right, 0.0f);
    rotationScale[1] = glm::vec4(up, 0.0f);
    rotationScale[2] = glm::vec4(-forward, 0.0f);
    rotationScale =
        glm::rotate(rotationScale, glm::radians(heldItemConfig.blockRotationDegrees.x + animRotation.x),
                    glm::vec3(1.0f, 0.0f, 0.0f));
    rotationScale =
        glm::rotate(rotationScale, glm::radians(heldItemConfig.blockRotationDegrees.y + animRotation.y),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    rotationScale =
        glm::rotate(rotationScale, glm::radians(heldItemConfig.blockRotationDegrees.z + animRotation.z),
                    glm::vec3(0.0f, 0.0f, 1.0f));
    rotationScale = glm::rotate(rotationScale, std::sin(time * 1.6f) * 0.08f,
                                glm::vec3(0.0f, 1.0f, 0.0f));
    rotationScale =
        glm::rotate(rotationScale, glm::radians(22.0f) * (1.0f - shownAmount),
                    glm::vec3(0.0f, 0.0f, 1.0f));
    rotationScale = glm::scale(rotationScale,
                               glm::vec3(heldItemConfig.blockScale *
                                         blockModel->asset.fitScale));

    entityShader.use();
    entityShader.setMat4("uVP", vp);
    entityShader.setVec3("uCameraPos", player.camera.position);
    entityShader.setVec4("uFogColor", getFogColor(depth));
    entityShader.setFloat("uFogDensity", 0.0f);
    entityShader.setFloat("uAlpha", shownAmount);
    entityShader.setInt("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    drawLoadedObjBlockModel(
        *blockModel,
        buildCenteredBlockModelTransform(*blockModel, position, rotationScale));

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Renderer::renderHeldBlock(const Player& player, const FractalWorld* world,
                               BlockId heldType, const glm::mat4& vp, int depth,
                               float time, float visibility, float verticalOffset) {
    const glm::vec3 forward = glm::normalize(player.camera.getForward());
    const glm::vec3 right = glm::normalize(player.camera.getRight());
    const glm::vec3 up = glm::normalize(player.camera.getUp());
    const auto& heldItemConfig = HeldItemDebugView::config;
    const auto& handAnim = HandAnimation::state;
    const auto& handConfig = HandAnimation::config;
    const float shownAmount = visibility * visibility * (3.0f - 2.0f * visibility);
    const float idleLift = std::sin(time * handConfig.idleSwaySpeed) * handConfig.idleSwayAmount;
    const float hiddenOffset = (1.0f - shownAmount) * 0.72f;

    const glm::vec3 animTranslation = handAnim.getTranslationOffset(handConfig);
    const glm::vec3 position =
        player.camera.position + forward * heldItemConfig.blockPositionOffset.x +
        right * heldItemConfig.blockPositionOffset.y +
        up * (heldItemConfig.blockPositionOffset.z - hiddenOffset + idleLift +
              verticalOffset) +
        forward * animTranslation.x +
        right * animTranslation.y +
        up * animTranslation.z;

    const glm::vec3 animRotation = handAnim.getRotationOffsetDegrees(handConfig);
    glm::mat4 rotation(1.0f);
    rotation[0] = glm::vec4(right, 0.0f);
    rotation[1] = glm::vec4(up, 0.0f);
    rotation[2] = glm::vec4(-forward, 0.0f);
    rotation = glm::rotate(rotation, glm::radians(heldItemConfig.blockRotationDegrees.x + animRotation.x),
                           glm::vec3(1.0f, 0.0f, 0.0f));
    rotation = glm::rotate(rotation, glm::radians(heldItemConfig.blockRotationDegrees.y + animRotation.y),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, glm::radians(heldItemConfig.blockRotationDegrees.z + animRotation.z),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    rotation = glm::rotate(rotation, std::sin(time * 1.6f) * 0.08f,
                           glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, glm::radians(22.0f) * (1.0f - shownAmount),
                           glm::vec3(0.0f, 0.0f, 1.0f));

    const glm::vec3 blockOrigin(0.0f);
    uploadBlockCube(heldType, blockOrigin, depth);
    const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), position) * rotation *
        glm::scale(glm::mat4(1.0f), glm::vec3(heldItemConfig.blockScale)) *
        glm::translate(glm::mat4(1.0f), -(blockOrigin + glm::vec3(0.5f)));

    blockShader.use();
    blockShader.setMat4("uVP", vp);
    blockShader.setMat4("uModel", model);
    blockShader.setVec3("uCameraPos", player.camera.position);
    blockShader.setVec4("uFogColor", getFogColor(depth));
    blockShader.setFloat("uFogDensity", 0.0f);
    blockShader.setFloat("uTime", time);
    blockShader.setFloat("uAlpha", shownAmount);
    blockShader.setFloat("uAoStrength", 1.0f);
    blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(world, depth));
    blockShader.setInt("uUseLocalMaterialSpace", 1);
    blockShader.setInt("uPointLightCount", 0);
    bindBlockTextures();
    setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
    setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);

    glBindVertexArray(breakBlockVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void Renderer::setupDustParticles() {
    const auto& dust = DustParticles::config;

    glGenVertexArrays(1, &dustParticleVAO);
    glGenBuffers(1, &dustParticleVBO);
    glBindVertexArray(dustParticleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, dustParticleVBO);
    dustParticleCapacity = dust.initialBufferCapacity;
    glBufferData(GL_ARRAY_BUFFER, sizeof(DustParticleVertex) * dustParticleCapacity, nullptr,
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DustParticleVertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(DustParticleVertex),
                          reinterpret_cast<void*>(offsetof(DustParticleVertex, alpha)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(DustParticleVertex),
                          reinterpret_cast<void*>(offsetof(DustParticleVertex, size)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Renderer::setupBlockBreakParticles() {
    glGenVertexArrays(1, &blockBreakParticleVAO);
    glGenBuffers(1, &blockBreakParticleVBO);
    glBindVertexArray(blockBreakParticleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, blockBreakParticleVBO);

    blockBreakParticleVertexCapacity = kBreakingParticleInitialVertexCapacity;
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(BlockBreakParticleVertex) * blockBreakParticleVertexCapacity,
        nullptr,
        GL_DYNAMIC_DRAW
    );

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BlockBreakParticleVertex),
        reinterpret_cast<void*>(offsetof(BlockBreakParticleVertex, position))
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BlockBreakParticleVertex),
        reinterpret_cast<void*>(offsetof(BlockBreakParticleVertex, normal))
    );
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BlockBreakParticleVertex),
        reinterpret_cast<void*>(offsetof(BlockBreakParticleVertex, color))
    );
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        3,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BlockBreakParticleVertex),
        reinterpret_cast<void*>(offsetof(BlockBreakParticleVertex, ao))
    );
    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        4,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BlockBreakParticleVertex),
        reinterpret_cast<void*>(offsetof(BlockBreakParticleVertex, material))
    );
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
}

std::uint32_t Renderer::hash4i(int x, int y, int z, int w) const {
    std::uint32_t hash = 0x9E3779B9u;
    hash ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    hash ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
    hash ^= static_cast<std::uint32_t>(z) * 0x27D4EB2Fu;
    hash ^= static_cast<std::uint32_t>(w) * 0x165667B1u;
    hash ^= hash >> 16;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15;
    hash *= 0x846CA68Bu;
    hash ^= hash >> 16;
    return hash;
}

float Renderer::hash01(std::uint32_t value) const {
    return static_cast<float>(value & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void Renderer::emitBlockBreakParticles(
    const glm::ivec3& blockPosition,
    BlockId blockType,
    const glm::ivec3& blockNormal
) {
    if (blockType == BlockIds::AIR) {
        return;
    }

    spawnBlockBreakParticles(
        blockPosition,
        blockType,
        blockNormal,
        kBreakingParticleFinishCount,
        true
    );
}

void Renderer::spawnBlockBreakParticles(
    const glm::ivec3& blockPosition,
    BlockId blockType,
    const glm::ivec3& blockNormal,
    int particleCount,
    bool finishBurst
) {
    if (blockType == BlockIds::AIR || particleCount <= 0) {
        return;
    }

    const glm::vec3 blockCenter = glm::vec3(blockPosition) + glm::vec3(0.5f);
    const std::uint32_t baseSerial = blockBreakParticleTracking.emissionSerial++;

    for (int index = 0; index < particleCount; ++index) {
        const std::uint32_t hash =
            hash4i(blockPosition.x, blockPosition.y, blockPosition.z,
                   static_cast<int>(baseSerial * 131u + static_cast<std::uint32_t>(index)));

        glm::vec3 normal = glm::vec3(blockNormal);
        if (glm::dot(normal, normal) <= 1e-6f) {
            normal = chooseFallbackFaceNormal(hash);
        } else {
            normal = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        if (finishBurst && (hash & 1u) != 0u) {
            normal = randomUnitDirection(
                hash01(hash ^ 0x52F4A713u),
                hash01(hash ^ 0x8C2C5C5Du)
            );
        }

        glm::vec3 tangent(1.0f, 0.0f, 0.0f);
        glm::vec3 bitangent(0.0f, 0.0f, 1.0f);
        buildParticleBasis(normal, tangent, bitangent);

        const float offsetU = (hash01(hash ^ 0x11C9F3A5u) - 0.5f) *
            kBreakingParticleFaceSpread;
        const float offsetV = (hash01(hash ^ 0x3D7E5B21u) - 0.5f) *
            kBreakingParticleFaceSpread;
        const float faceJitter = finishBurst
            ? (hash01(hash ^ 0x5F7B9D31u) - 0.5f) * 0.32f
            : 0.0f;

        BlockBreakParticle particle{};
        particle.position =
            blockCenter +
            normal * (kBreakingParticleFaceOffset + faceJitter) +
            tangent * offsetU +
            bitangent * offsetV;
        particle.normal = normal;
        particle.tangent = tangent;
        particle.bitangent = bitangent;
        particle.blockType = blockType;
        particle.lifetime =
            (finishBurst ? kBreakingParticleFinishLifetime : kBreakingParticleBaseLifetime) *
            (0.82f + hash01(hash ^ 0xA1F62B99u) * 0.42f);
        particle.size = glm::mix(
            finishBurst ? kBreakingParticleFinishMinSize : kBreakingParticleMinSize,
            finishBurst ? kBreakingParticleFinishMaxSize : kBreakingParticleMaxSize,
            hash01(hash ^ 0xC4B1E72Du)
        );
        particle.rotation = hash01(hash ^ 0x9C9277B5u) * kTwoPi;
        particle.angularVelocity =
            (hash01(hash ^ 0x735A2D13u) - 0.5f) * (finishBurst ? 16.0f : 9.0f);

        const float speed =
            (finishBurst ? kBreakingParticleFinishSpeed : kBreakingParticleBaseSpeed) *
            (0.65f + hash01(hash ^ 0xE4137AA7u) * 0.75f);
        particle.velocity =
            normal * speed +
            tangent * ((hash01(hash ^ 0x8DBB792Fu) - 0.5f) * speed * 0.55f) +
            bitangent * ((hash01(hash ^ 0x2B2766C5u) - 0.5f) * speed * 0.55f) +
            glm::vec3(0.0f, finishBurst ? 0.75f : 0.22f, 0.0f);

        blockBreakParticles_.push_back(particle);
    }

    if (blockBreakParticles_.size() > kBreakingParticleMaxCount) {
        const std::size_t overflow =
            blockBreakParticles_.size() - kBreakingParticleMaxCount;
        blockBreakParticles_.erase(
            blockBreakParticles_.begin(),
            blockBreakParticles_.begin() + static_cast<std::ptrdiff_t>(overflow)
        );
    }
}

void Renderer::updateBlockBreakParticles(
    const Player& player,
    FractalWorld* world,
    int depth,
    float dt
) {
    const std::uint64_t currentWorldKey = makeDustWorldKey(world, depth);
    if (!blockBreakParticleTracking.initialized) {
        blockBreakParticleTracking.initialized = true;
        blockBreakParticleTracking.worldKey = currentWorldKey;
    }

    if (currentWorldKey != blockBreakParticleTracking.worldKey) {
        blockBreakParticleTracking = {};
        blockBreakParticleTracking.initialized = true;
        blockBreakParticleTracking.worldKey = currentWorldKey;
        blockBreakParticles_.clear();
        blockBreakParticleVertices_.clear();
    }

    for (BlockBreakParticle& particle : blockBreakParticles_) {
        particle.age += dt;
        particle.velocity.y -= kBreakingParticleGravity * dt;
        particle.velocity *= std::pow(kBreakingParticleDrag, glm::max(dt * 60.0f, 0.0f));
        particle.position += particle.velocity * dt;
        particle.rotation += particle.angularVelocity * dt;
    }

    blockBreakParticles_.erase(
        std::remove_if(
            blockBreakParticles_.begin(),
            blockBreakParticles_.end(),
            [](const BlockBreakParticle& particle) {
                return particle.age >= particle.lifetime;
            }
        ),
        blockBreakParticles_.end()
    );

    const bool breakingTarget =
        world &&
        player.hasTargetBlock() &&
        player.isBreakingTargetBlock() &&
        player.getBreakingBlock() == player.getTargetBlock();

    if (!breakingTarget) {
        blockBreakParticleTracking.tracking = false;
        return;
    }

    const glm::ivec3 blockPosition = player.getBreakingBlock();
    const BlockId blockType = player.getBreakingBlockType();
    const glm::ivec3 blockNormal = player.getTargetNormal();
    const float progress = player.getBreakingProgress();

    const bool changedTarget =
        !blockBreakParticleTracking.tracking ||
        blockBreakParticleTracking.blockPosition != blockPosition ||
        blockBreakParticleTracking.blockType != blockType;

    if (changedTarget) {
        blockBreakParticleTracking.tracking = true;
        blockBreakParticleTracking.blockPosition = blockPosition;
        blockBreakParticleTracking.blockNormal = blockNormal;
        blockBreakParticleTracking.blockType = blockType;
        blockBreakParticleTracking.lastProgress = progress;
        blockBreakParticleTracking.nextProgressEmission = kBreakingParticleProgressInterval;
        blockBreakParticleTracking.emissionTimer = 0.0f;

        spawnBlockBreakParticles(
            blockPosition,
            blockType,
            blockNormal,
            kBreakingParticleStartCount,
            false
        );
        return;
    }

    blockBreakParticleTracking.blockNormal = blockNormal;
    blockBreakParticleTracking.emissionTimer += dt;

    if (progress >= blockBreakParticleTracking.nextProgressEmission ||
        blockBreakParticleTracking.emissionTimer >= kBreakingParticleEmissionInterval) {
        spawnBlockBreakParticles(
            blockPosition,
            blockType,
            blockNormal,
            kBreakingParticleProgressCount,
            false
        );

        blockBreakParticleTracking.nextProgressEmission =
            glm::min(progress + kBreakingParticleProgressInterval, 1.0f);
        blockBreakParticleTracking.emissionTimer = 0.0f;
    }

    blockBreakParticleTracking.lastProgress = progress;
}

void Renderer::renderBlockBreakParticles(
    const FractalWorld& world,
    const Camera& cam,
    const glm::mat4& vp,
    const glm::vec4& fog,
    int depth,
    int renderDistance,
    float time
) {
    if (blockBreakParticles_.empty()) {
        return;
    }

    blockBreakParticleVertices_.clear();
    blockBreakParticleVertices_.reserve(blockBreakParticles_.size() * 6);

    for (const BlockBreakParticle& particle : blockBreakParticles_) {
        appendBlockBreakParticleQuad(blockBreakParticleVertices_, particle, depth);
    }

    if (blockBreakParticleVertices_.empty()) {
        return;
    }

    if (blockBreakParticleVertices_.size() > blockBreakParticleVertexCapacity) {
        blockBreakParticleVertexCapacity = blockBreakParticleVertices_.size() + 384;
        glBindBuffer(GL_ARRAY_BUFFER, blockBreakParticleVBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            sizeof(BlockBreakParticleVertex) * blockBreakParticleVertexCapacity,
            nullptr,
            GL_DYNAMIC_DRAW
        );
    }

    blockShader.use();
    blockShader.setMat4("uVP", vp);
    blockShader.setMat4("uModel", glm::mat4(1.0f));
    blockShader.setVec3("uCameraPos", cam.position);
    blockShader.setVec4("uFogColor", fog);
    blockShader.setFloat("uFogDensity", computeFogDensity(depth, renderDistance));
    blockShader.setFloat("uTime", time);
    blockShader.setFloat("uAlpha", 1.0f);
    blockShader.setFloat("uAoStrength", 1.0f);
    blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(&world, depth));
    blockShader.setInt("uUseLocalMaterialSpace", 0);
    blockShader.setInt("uPointLightCount", 0);
    bindBlockTextures();
    setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
    setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(blockBreakParticleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, blockBreakParticleVBO);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        blockBreakParticleVertices_.size() * sizeof(BlockBreakParticleVertex),
        blockBreakParticleVertices_.data()
    );
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(blockBreakParticleVertices_.size()));
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
}

void Renderer::renderDustParticles(FractalWorld& world, const Camera& cam,
                                   const glm::mat4& vp, const glm::vec4& fog, int depth,
                                   float time, float visibilityAlpha) {
    const auto& dust = DustParticles::config;
    const float cellSize = dust.cellSize;
    const float radius = dust.radius;
    const int particlesPerCell = dust.particlesPerCell;
    const glm::ivec3 centerCell = glm::ivec3(glm::floor(cam.position / cellSize));
    const int cellRadiusXZ = static_cast<int>(std::ceil(radius / cellSize));
    const int cellRadiusY = dust.cellRadiusY;

    dustParticleScratch.clear();
    dustParticleScratch.reserve((cellRadiusXZ * 2 + 1) * (cellRadiusXZ * 2 + 1) *
                                (cellRadiusY * 2 + 1) * particlesPerCell);

    for (int cx = centerCell.x - cellRadiusXZ; cx <= centerCell.x + cellRadiusXZ; cx++) {
        for (int cy = centerCell.y - cellRadiusY; cy <= centerCell.y + cellRadiusY; cy++) {
            for (int cz = centerCell.z - cellRadiusXZ; cz <= centerCell.z + cellRadiusXZ;
                 cz++) {
                for (int index = 0; index < particlesPerCell; index++) {
                    const std::uint32_t hash = hash4i(cx, cy, cz, index + depth * 31);
                    const float rx = hash01(hash ^ 0x68BC21EBu);
                    const float ry = hash01(hash ^ 0x02E5BE93u);
                    const float rz = hash01(hash ^ 0x967A889Bu);
                    const float phase = hash01(hash ^ 0xB5297A4Du) * 6.2831853f;
                    const float sway =
                        dust.minSway +
                        hash01(hash ^ 0x1B56C4E9u) * (dust.maxSway - dust.minSway);
                    const float speed =
                        dust.minSpeed +
                        hash01(hash ^ 0x7F4A7C15u) * (dust.maxSpeed - dust.minSpeed);

                    glm::vec3 position((static_cast<float>(cx) + rx) * cellSize,
                                       (static_cast<float>(cy) + ry) * cellSize,
                                       (static_cast<float>(cz) + rz) * cellSize);

                    position.x += std::sin(time * speed + phase) * sway;
                    position.y +=
                        std::sin(time * speed * dust.verticalSpeedMultiplier + phase * 1.7f) *
                        sway * dust.verticalSwayMultiplier;
                    position.z +=
                        std::cos(time * speed * dust.depthSpeedMultiplier + phase * 1.3f) *
                        sway;

                    const glm::vec3 delta = position - cam.position;
                    const float dist2 = glm::dot(delta, delta);
                    if (dist2 > radius * radius) {
                        continue;
                    }

                    if (isSolid(world.getBlock(glm::ivec3(glm::floor(position))))) {
                        continue;
                    }

                    const float distFade =
                        1.0f - glm::clamp(std::sqrt(dist2) / radius, 0.0f, 1.0f);
                    DustParticleVertex vertex{};
                    vertex.position = position;
                    vertex.alpha = dust.baseAlpha + distFade * dust.distanceAlphaBoost;
                    vertex.size =
                        dust.minSize +
                        hash01(hash ^ 0xA24BAEDCu) * (dust.maxSize - dust.minSize);
                    dustParticleScratch.push_back(vertex);
                }
            }
        }
    }

    if (dustParticleScratch.empty()) {
        return;
    }

    if (dustParticleScratch.size() > dustParticleCapacity) {
        dustParticleCapacity = dustParticleScratch.size() + 256;
        glBindBuffer(GL_ARRAY_BUFFER, dustParticleVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(DustParticleVertex) * dustParticleCapacity,
                     nullptr, GL_DYNAMIC_DRAW);
    }

    const glm::vec3 dustColor =
        glm::mix(glm::vec3(fog), dust.colorTint, dust.fogColorBlend);

    dustParticleShader.use();
    dustParticleShader.setMat4("uVP", vp);
    dustParticleShader.setVec3("uCameraPos", cam.position);
    dustParticleShader.setVec4("uColor", glm::vec4(dustColor, visibilityAlpha));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    glBindVertexArray(dustParticleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, dustParticleVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    dustParticleScratch.size() * sizeof(DustParticleVertex),
                    dustParticleScratch.data());
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(dustParticleScratch.size()));
    glBindVertexArray(0);

    glDisable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::renderCrosshair() {
    lineShader.use();
    lineShader.setMat4("uMVP", glm::mat4(1.0f));
    lineShader.setVec4("uColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.85f));
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glBindVertexArray(crosshairVAO);
    glDrawArrays(GL_LINES, 0, 4);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::setBreakEffectUniforms(const glm::vec3& blockCenter, float breakProgress) {
    blockShader.setVec3("uBreakBlockCenter", blockCenter);
    blockShader.setFloat("uBreakProgress", breakProgress);
}

void Renderer::setHighlightEffectUniforms(const glm::vec3& blockCenter, float active) {
    blockShader.setVec3("uHighlightBlockCenter", blockCenter);
    blockShader.setFloat("uHighlightActive", active);
}

void Renderer::uploadBlockCube(BlockId type, const glm::vec3& blockOrigin, int depth) {
    static const glm::vec3 normals[6] = {
        {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
    };
    static const glm::vec3 faceVertices[6][6] = {
        {{0, 0, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{1, 0, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 0}, {0, 1, 0}},
        {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    };

    std::array<Chunk::Vertex, 36> vertices{};
    std::size_t vertexIndex = 0;

    for (int face = 0; face < 6; face++) {
        const glm::vec4 color = getBlockColor(type, depth, face);
        const float material = getBlockMaterialId(type);
        for (int index = 0; index < 6; index++) {
            vertices[vertexIndex++] = {blockOrigin + faceVertices[face][index],
                                       normals[face], color, 1.0f, material};
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, breakBlockVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
}

void Renderer::renderDroppedItems(const FractalWorld& world, const glm::mat4& vp,
                                  const glm::vec3& cameraPos, const glm::vec4& fog, int depth,
                                  int renderDistance, float time, float alpha) {
    if (world.droppedItems.empty()) {
        return;
    }

    const auto bindDroppedBlockState = [&]() {
        blockShader.use();
        blockShader.setMat4("uVP", vp);
        blockShader.setMat4("uModel", glm::mat4(1.0f));
        blockShader.setVec3("uCameraPos", cameraPos);
        blockShader.setVec4("uFogColor", fog);
        blockShader.setFloat("uFogDensity", computeFogDensity(depth, renderDistance));
        blockShader.setFloat("uTime", time);
        blockShader.setFloat("uAlpha", alpha);
        blockShader.setFloat("uAoStrength", 1.0f);
        blockShader.setVec4("uBiomeTint", getBiomeMaterialTint(&world, depth));
        blockShader.setInt("uUseLocalMaterialSpace", 1);
        bindBlockTextures();
        setBreakEffectUniforms(glm::vec3(0.0f), 0.0f);
        setHighlightEffectUniforms(glm::vec3(0.0f), 0.0f);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glBindVertexArray(breakBlockVAO);
    };

    bindDroppedBlockState();
    for (const FractalWorld::DroppedItem& item : world.droppedItems) {
        if (glm::length(item.position - cameraPos) > 28.0f) {
            continue;
        }

        if (item.item.isBlock() && usesCustomBlockModel(item.item.blockType)) {
            const LoadedObjBlockModel* blockModel =
                getLoadedCustomBlockModel(item.item.blockType);
            if (!blockModel) {
                continue;
            }

            glm::mat4 rotationScale(1.0f);
            rotationScale = glm::rotate(rotationScale, time * 1.9f + item.spinPhase,
                                        glm::normalize(glm::vec3(1.0f, 0.3f, 0.1f)));
            rotationScale = glm::rotate(rotationScale,
                                        time * 2.4f + item.spinPhase * 1.7f,
                                        glm::normalize(glm::vec3(0.2f, 1.0f, 0.4f)));
            rotationScale = glm::rotate(rotationScale,
                                        time * 2.1f + item.spinPhase * 2.3f,
                                        glm::normalize(glm::vec3(0.4f, 0.2f, 1.0f)));
            rotationScale = glm::scale(rotationScale,
                                       glm::vec3(0.24f * blockModel->asset.fitScale));

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

            drawLoadedObjBlockModel(
                *blockModel,
                buildCenteredBlockModelTransform(
                    *blockModel, buildDroppedModelCenter(item.position, time, item.spinPhase),
                    rotationScale));

            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            bindDroppedBlockState();
            continue;
        }

        if (item.item.isBlock() && isSolid(item.item.blockType)) {
            glm::mat4 rotation(1.0f);
            rotation = glm::rotate(rotation, time * 1.9f + item.spinPhase,
                                   glm::normalize(glm::vec3(1.0f, 0.3f, 0.1f)));
            rotation = glm::rotate(rotation, time * 2.4f + item.spinPhase * 1.7f,
                                   glm::normalize(glm::vec3(0.2f, 1.0f, 0.4f)));
            rotation = glm::rotate(rotation, time * 2.1f + item.spinPhase * 2.3f,
                                   glm::normalize(glm::vec3(0.4f, 0.2f, 1.0f)));
            const glm::vec3 blockOrigin = glm::floor(item.position);
            uploadBlockCube(item.item.blockType, blockOrigin, depth);
            const glm::mat4 model =
                glm::translate(glm::mat4(1.0f), item.position) * rotation *
                glm::scale(glm::mat4(1.0f), glm::vec3(0.24f)) *
                glm::translate(glm::mat4(1.0f), -(blockOrigin + glm::vec3(0.5f)));
            blockShader.setMat4("uModel", model);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            continue;
        }

        if (!usesItemTexturePreview(item.item)) {
            continue;
        }

        glBindVertexArray(0);
        renderItemSprite(item.item.itemId,
                         vp,
                         buildDroppedSpriteModel(item.position, cameraPos, time,
                                                 item.spinPhase),
                         alpha, true);
        bindDroppedBlockState();
    }
    glBindVertexArray(0);
}
