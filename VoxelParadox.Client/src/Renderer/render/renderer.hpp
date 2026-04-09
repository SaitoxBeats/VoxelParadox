// renderer.hpp
// Public map of the renderer. Read this file before opening the renderer .cpp files.

#pragma once

// 1. Standard Library
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// 2. Third-party Libraries
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

// 3. Local Project Modules
#include "engine/shader.hpp"
#include "entities/fbx_entity_model.hpp"
#include "item_preview_config.hpp"
#include "items/item_catalog.hpp"
#include "obj_block_model.hpp"
#include "player/player.hpp"

class Camera;
class FractalWorld;
class WorldStack;

// Renderer is the 3D frame owner for the client.
// If you come from Unity, read this as the equivalent of a render manager plus
// a few small render subsystems that were grouped into one class.
class Renderer {
public:
    // --- 1. Core Data Structures ---

    struct PortalFrame {
        glm::vec3 center{ 0.0f };
        glm::vec3 right{ 1.0f, 0.0f, 0.0f };
        glm::vec3 up{ 0.0f, 1.0f, 0.0f };
        glm::vec3 front{ 0.0f, 0.0f, 1.0f };
    };

    struct DustParticleVertex {
        glm::vec3 position{ 0.0f };
        float alpha = 0.0f;
        float size = 2.0f;
    };

    struct DustTransitionState {
        bool initialized = false;
        std::uint64_t worldKey = 0;
        float visibility = 1.0f;
        bool waitingForWorldLoad = false;
    };

    struct HeldItemTransitionState {
        enum class SwapPhase {
            NONE,
            HIDING,
            SHOWING,
        };

        bool initialized = false;
        std::uint64_t worldKey = 0;
        float visibility = 1.0f;
        bool waitingForWorldLoad = false;
        int selectedIndex = -1;
        InventoryItem currentItem{};
        InventoryItem swapFromItem{};
        InventoryItem swapToItem{};
        float swapTimer = 0.0f;
        float swapDuration = 0.18f;
        float swapVerticalOffset = 0.0f;
        bool swapAnimating = false;
        SwapPhase swapPhase = SwapPhase::NONE;
    };

    struct SceneRenderTarget {
        GLuint framebuffer = 0;
        GLuint colorTexture = 0;
        GLuint depthStencilRenderbuffer = 0;
        glm::ivec2 size{ 0 };
    };

    // Create an IMGUI here to get a visual idea.. trying to do it in your head can really wear you down mentally
    struct HeldItemViewDebugConfig {
        glm::vec3 blockPositionOffset{ 0.485f, 0.425f, -0.30f };
        glm::vec3 blockRotationDegrees{ -16.0f, 30.0f, 12.0f };
        float blockScale = 0.33f;
        glm::vec3 spritePositionOffset{ 0.645f, 0.470f, -0.330f };
        glm::vec3 spriteRotationDegrees{ 0.0f, -91.2f, 38.8f };
        float spriteScale = 0.785f;
    };

    struct LoadedEntityModel {
        struct Part {
            EntityModelPartAsset asset{};
            GLuint vao = 0;
            GLuint vbo = 0;

            bool ready() const {
                return asset.valid() && vao != 0 && vbo != 0;
            }
        };

        EntityModelAsset asset{};
        std::vector<Part> parts{};
        GLuint texture = 0;

        bool ready() const {
            return asset.valid() && !parts.empty() && texture != 0;
        }
    };

    struct LoadedObjBlockModel {
        struct Part {
            ObjBlockModelPartAsset asset{};
            GLuint vao = 0;
            GLuint vbo = 0;

            bool ready() const {
                return asset.valid() && vao != 0 && vbo != 0;
            }
        };

        ObjBlockModelAsset asset{};
        std::vector<Part> parts{};
        GLuint texture = 0;

        bool ready() const {
            return asset.valid() && !parts.empty() && texture != 0;
        }
    };

    // --- 2. Public API ---

    bool init();
    void cleanup();

    void setRenderScale(float scale);
    float getRenderScale() const { return renderScale_; }

    void render(WorldStack& worldStack, Player& player, float aspect, float time,
        bool wireframeMode = false, bool debugThirdPersonView = false);

    void renderHUD3DOverlays(const Player& player, const FractalWorld* world, int depth, float time);
    void renderDeathScreenBackground(const glm::ivec2& screenSize, float timeSeconds,
        float vignetteExtra);

    void renderItemPreviewInRect(const glm::ivec4& slotRectTopLeft, int screenHeight, int inset,
        const InventoryItem& item, int depth, float time,
        const HUDItemPreviewConfig& config, bool spinning = false,
        float scaleMultiplier = 1.0f, const FractalWorld* world = nullptr);

    void renderDepthIndicator(int depth);

private:
    // --- 3. GPU & Render State ---

    Shader blockShader{};
    Shader lineShader{};
    Shader depthWindowShader{};
    Shader dustParticleShader{};
    Shader itemSpriteShader{};
    Shader entityShader{};
    Shader deathScreenShader{};

    GLuint crosshairVAO = 0;
    GLuint crosshairVBO = 0;
    GLuint breakBlockVAO = 0;
    GLuint breakBlockVBO = 0;
    GLuint stencilFaceVAO = 0;
    GLuint stencilFaceVBO = 0;
    GLuint portalFrameVAO = 0;
    GLuint portalFrameVBO = 0;
    GLuint screenQuadVAO = 0;
    GLuint screenQuadVBO = 0;
    GLuint itemSpriteVAO = 0;
    GLuint itemSpriteVBO = 0;
    GLuint dustParticleVAO = 0;
    GLuint dustParticleVBO = 0;

    std::size_t dustParticleCapacity = 0;
    DustTransitionState dustTransition{};
    HeldItemTransitionState heldItemTransition{};

    LoadedEntityModel guyModel_{};
    LoadedObjBlockModel membraneWireModel_{};

    std::unordered_map<int, GLuint> itemTextureCache{};
    std::vector<DustParticleVertex> dustParticleScratch{};
    SceneRenderTarget sceneRenderTarget_{};
    float renderScale_ = 1.0f;

    // --- 4. Atmosphere & Transitions ---

    glm::vec4 getBiomeMaterialTint(const FractalWorld* world, int depth) const;
    glm::vec4 getFogColor(int depth);
    float computeFogDensity(int depth, int renderDistance) const;

    std::uint64_t makeDustWorldKey(const FractalWorld* world, int depth) const;
    void updateDustTransition(FractalWorld* world, const Player& player, int depth, float dt);
    void updateHeldItemTransition(FractalWorld* world, const Player& player, int depth, float dt);

    // --- 5. HUD, Previews & Items ---

    glm::ivec4 buildPreviewRect(const glm::ivec4& slotRectTopLeft, int screenHeight, int inset) const;
    void renderHotbarPreviews(const Player& player, const FractalWorld* world, int depth, float time);

    glm::mat4 buildPreviewItemModel(const HUDItemPreviewConfig& config, float time,
        bool spinning, float scaleMultiplier) const;

    void setupBreakBlockCube();
    void setupItemSpriteQuad();
    GLuint getItemTexture(ItemType type);

    void renderItemSprite(ItemType type, const glm::mat4& vp, const glm::mat4& model,
        float alpha, bool depthTest);

    void renderHeldItem(const Player& player, const FractalWorld* world, const glm::mat4& vp,
        int depth, float time, float visibility);

    void renderHeldSpriteItem(const Player& player, ItemType heldType, const glm::mat4& vp,
        float time, float visibility, float verticalOffset);

    void renderHeldCustomBlockModel(const Player& player, const FractalWorld* world,
        BlockType heldType, const glm::mat4& vp, int depth,
        float time, float visibility, float verticalOffset);

    void renderHeldBlock(const Player& player, const FractalWorld* world, BlockType heldType,
        const glm::mat4& vp, int depth, float time, float visibility,
        float verticalOffset);

    void renderDroppedItems(const FractalWorld& world, const glm::mat4& vp,
        const glm::vec3& cameraPos, const glm::vec4& fog, int depth,
        int renderDistance, float time, float alpha);

    // --- 6. Particles & Entities ---

    void setupDustParticles();
    std::uint32_t hash4i(int x, int y, int z, int w) const;
    float hash01(std::uint32_t value) const;
    void renderDustParticles(FractalWorld& world, const Camera& cam, const glm::mat4& vp,
        const glm::vec4& fog, int depth, float time, float visibilityAlpha);

    bool setupEntityModel(LoadedEntityModel& model, const EntityModelAsset& asset);
    bool setupObjBlockModel(LoadedObjBlockModel& model, const ObjBlockModelAsset& asset);
    bool setupEntityAssets();
    bool setupBlockModelAssets();
    void cleanupEntityAssets();
    void cleanupBlockModelAssets();

    const LoadedObjBlockModel* getLoadedCustomBlockModel(BlockType type) const;
    float getCustomBlockModelFitScale(BlockType type) const;

    glm::mat4 buildWorldBlockModelTransform(const LoadedObjBlockModel& model,
        const glm::vec3& cellOrigin,
        float yRotationRadians) const;

    glm::mat4 buildCenteredBlockModelTransform(const LoadedObjBlockModel& model,
        const glm::vec3& cellCenter,
        const glm::mat4& rotationScale) const;

    void drawLoadedObjBlockModel(const LoadedObjBlockModel& model, const glm::mat4& modelMatrix);

    void renderEntities(const FractalWorld& world, const glm::mat4& vp,
        const glm::vec3& cameraPos, const glm::vec4& fog, int depth,
        int renderDistance, float time, float alpha = 1.0f);

    void renderWorldBlockModels(const FractalWorld& world, const glm::mat4& vp,
        const glm::vec3& cameraPos, const glm::vec4& fog, int depth,
        int renderDistance, float time, float alpha = 1.0f);

    // --- 7. Geometry, Portals & Stencils ---

    void renderTargetSelectionWireframe(const FractalWorld& world, const Player& player, const glm::mat4& vp);
    void renderCrosshair();
    void setBreakEffectUniforms(const glm::vec3& blockCenter, float breakProgress);
    void setHighlightEffectUniforms(const glm::vec3& blockCenter, float active);
    void uploadBlockCube(BlockType type, const glm::vec3& blockOrigin, int depth);

    void setupStencilFaceQuad();
    void setupPortalFrameGeometry();
    void buildFaceBasis(glm::ivec3 faceNormal, glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent);

    void updatePortalFaceQuad(glm::ivec3 blockPos, glm::ivec3 faceNormal, float halfSize, float planeOffset);
    int updatePortalFrameGeometry(glm::ivec3 blockPos, glm::ivec3 faceNormal, bool innerWalls);

    void renderStencilMask(const glm::mat4& vp, glm::ivec3 blockPos, glm::ivec3 faceNormal);
    void prepareStencilDepthWindow();

    void renderNestedPreviewWorld(WorldStack& worldStack, FractalWorld& nestedWorld,
        const Player::NestedPreviewPortal& portal,
        const Camera& playerCamera, float aspect, float time);

    void renderPortalFrame(const glm::mat4& vp, glm::ivec3 blockPos, glm::ivec3 faceNormal, float fade);

    // --- 8. Scene & Render Targets ---

    void setupScreenQuad();
    Camera buildThirdPersonDebugCamera(const Camera& source) const;
    void renderCameraFrustumDebug(const Camera& source, const glm::mat4& debugViewProjection);

    glm::ivec2 sceneRenderSizeFor(const glm::ivec2& outputSize) const;
    bool shouldUseScaledSceneTarget(const glm::ivec2& outputSize) const;
    void ensureSceneRenderTarget(const glm::ivec2& outputSize);
    void releaseSceneRenderTarget();

    void renderScene(WorldStack& worldStack, Player& player, float aspect, float time,
        bool wireframeMode, bool debugThirdPersonView);
};

// --- 9. Debug Namespaces ---

namespace HeldItemDebugView {
    inline Renderer::HeldItemViewDebugConfig config{};
}

namespace EnemyDebugView {
    inline bool headTriggerWireframeEnabled = false;
}
