// Player is the local first-person gameplay controller.
// Unity analogy:
// - Camera + CharacterController + block interaction + inventory bridge.
// - RuntimeApp owns the frame loop and calls Player::update(...).
// - The implementation is split across multiple .cpp files by responsibility.

#pragma once

#pragma region Includes

// 1. Standard Library
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Internal Engine/Core Modules
#include "engine/camera.hpp"
#include "engine/input.hpp"

// 4. Local Project Modules
#include "enemies/enemy_types.hpp"
#include "hotbar.hpp"
#include "items/item_catalog.hpp"
#include "world/world_stack.hpp"

#pragma endregion

enum class PlayerTransition { NONE, DIVING_IN, RISING_OUT };

enum class PlayerUpdateMode {
    FullGameplay,
    SimulationOnly,
    Frozen,
};

class GameAudioController;

class Player {
public:
#pragma region 1. Constants & Defaults
    // --- 1. Constants & Defaults ---

    static constexpr double kUniverseCreationCooldownSeconds = 600.0;
    static constexpr float kDefaultWalkSpeed = 4.317f;
    static constexpr float kDefaultRunSpeed = 5.612f;
    static constexpr float kDefaultCrouchSpeed = 1.295f;
    static constexpr float kDefaultGroundAcceleration = 26.0f;
    static constexpr float kDefaultAirAcceleration = 8.5f;
    static constexpr float kDefaultGroundDeceleration = 18.0f;
    static constexpr float kDefaultAirDeceleration = 2.25f;
    static constexpr float kDefaultGroundFriction = 9.0f;
    static constexpr float kDefaultJumpHeight = 1.25f;
    static constexpr float kDefaultDoubleJumpBoostHeight = 1.25f;
    static constexpr double kDoubleJumpCooldownSeconds = 5.0;
    static constexpr double kDoubleJumpWindowSeconds = 0.35;
    static constexpr float kDefaultGravityAcceleration = 6.8f;
    static constexpr float kDefaultPlayerRadius = 0.30f;
    static constexpr float kDefaultStandingHeight = 1.80f;
    static constexpr float kDefaultCrouchingHeight = 1.50f;
    static constexpr float kDefaultStandingEyeHeight = 1.62f;
    static constexpr float kDefaultCrouchingEyeHeight = 1.27f;
    static constexpr float kDefaultCrouchTransitionSpeed = 14.0f;

    // Default child-world spawn used by nested portal previews and traversal.
    static glm::vec3 nestedWorldSpawnPosition() {
        return glm::vec3(12.0f, 24.0f, 12.0f);
    }
#pragma endregion

#pragma region 2. Nested Data Structures
    // --- 2. Nested Data Structures ---

    struct NestedPreviewFrame {
        glm::vec3 center{ 0.0f };
        glm::vec3 right{ 1.0f, 0.0f, 0.0f };
        glm::vec3 up{ 0.0f, 1.0f, 0.0f };
        glm::vec3 front{ 0.0f, 0.0f, 1.0f };
    };

    struct NestedPreviewPortal {
        bool active = false;
        glm::ivec3 block{ 0 };
        glm::ivec3 normal{ 0 };
        float fade = 0.0f;
        bool hasOverrideFrame = false;
        NestedPreviewFrame overrideFrame{};
    };

    struct EnterNestedTransition {
        glm::ivec3 block{ 0 };
        glm::ivec3 normal{ 0 };
        glm::vec3 startPos{ 0.0f };
        glm::vec3 targetPos{ 0.0f };
        glm::quat startOrientation{ 1, 0, 0, 0 };
        glm::quat targetOrientation{ 1, 0, 0, 0 };
        glm::vec3 childPos{ 0.0f };
        glm::quat childOrientation{ 1, 0, 0, 0 };
        glm::vec3 parentReturnPos{ 0.0f };
        glm::quat parentReturnOrientation{ 1, 0, 0, 0 };
    };

    struct HeadBobSettings {
        bool enabled = true;
        float walkVerticalAmplitude = 0.0440f;
        float runVerticalAmplitude = 0.052f;
        float horizontalAmplitude = 0.0520f;
        float forwardAmplitude = 0.010f;
        float rollAmplitudeDegrees = 0.26f;
        float walkFrequency = 0.05f;
        float runFrequency = 1.65f;
        float crouchAmplitudeMultiplier = 0.70f;
        float crouchFrequencyMultiplier = 0.82f;
        float blendInSpeed = 10.0f;
        float blendOutSpeed = 7.0f;
    };

    struct FootstepSettings {
        bool enabled = true;
        float baseGain = 0.40f;
        float runGainMultiplier = 1.10f;
        float crouchGainMultiplier = 0.70f;
        float pitchMin = 0.97f;
        float pitchMax = 1.03f;
        float phaseOffsetDegrees = 0.0f;
        float minSpeed = 0.35f;
    };

    struct PersistentState {
        glm::vec3 cameraPosition{ 0.0f };
        glm::quat cameraOrientation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 velocity{ 0.0f };
        int lifePoints = 20;
        bool sandboxModeEnabled = false;
        double universeCreationCooldownRemainingSeconds = 0.0;
        double doubleJumpCooldownRemainingSeconds = 0.0;
        bool hasSpawnpoint = false;
        glm::vec3 spawnpointPosition{ 0.0f };
        std::uint32_t spawnpointUniverseSeed = 0;
        BiomeSelection spawnpointBiomeSelection{};
        std::vector<WorldLevel> spawnpointTraversalStack{};
        bool grounded = false;
        bool crouching = false;
        float currentEyeHeight = kDefaultStandingEyeHeight;
        float headBobPhase = 0.0f;
        float headBobBlend = 0.0f;
        glm::vec3 headBobLocalOffset{ 0.0f };
        float headBobRollRadians = 0.0f;
        float lastFootstepPhase = 0.0f;
        float damageRollTimer = 0.0f;
        float damageRollRadiansCurrent = 0.0f;
        float lifeFlashTimer = 0.0f;
        PlayerHotbar::PersistentState hotbarState{};
    };
#pragma endregion

#pragma region 3. Public State Variables
    // --- 3. Public State Variables ---
    // Runtime state used directly by rendering, input, and debug HUD.

    Camera camera;
    glm::vec3 velocity{ 0.0f };

    float walkSpeed = kDefaultWalkSpeed;
    float runSpeed = kDefaultRunSpeed;
    float crouchSpeed = kDefaultCrouchSpeed;
    float groundAcceleration = kDefaultGroundAcceleration;
    float airAcceleration = kDefaultAirAcceleration;
    float groundDeceleration = kDefaultGroundDeceleration;
    float airDeceleration = kDefaultAirDeceleration;
    float groundFriction = kDefaultGroundFriction;
    float jumpHeight = kDefaultJumpHeight;
    float doubleJumpBoostHeight = kDefaultDoubleJumpBoostHeight;
    float gravityAcceleration = kDefaultGravityAcceleration;
    float playerRadius = kDefaultPlayerRadius;
    float standingHeight = kDefaultStandingHeight;
    float crouchingHeight = kDefaultCrouchingHeight;
    float standingEyeHeight = kDefaultStandingEyeHeight;
    float crouchingEyeHeight = kDefaultCrouchingEyeHeight;
    float crouchTransitionSpeed = kDefaultCrouchTransitionSpeed;
    float breakRange = 8.0f;
    PlayerHotbar hotbar{};

    // Zoom
    float normalFov = 80.0f;
    float zoomedFov = 45.0f;
    float zoomSmoothSpeed = 10.0f;

    // Interaction & Target state
    bool hasTarget = false;
    glm::ivec3 targetBlock{ 0 };
    glm::ivec3 targetNormal{ 0 };
    bool isBreakingBlock = false;
    glm::ivec3 breakingBlock{ 0 };
    BlockType breakingBlockType = BlockType::AIR;
    float breakingTimer = 0.0f;
    float breakingProgress = 0.0f;
    float breakingHitCooldown = 0.0f;
    double nextDoubleJumpTimeSeconds = 0.0;
    double doubleJumpWindowExpiresSeconds = 0.0;
    NestedPreviewPortal nestedPreview;

    // Transitions
    PlayerTransition transition = PlayerTransition::NONE;
    float transitionTimer = 0.0f;
    float transitionDuration = 1.0f;
    glm::vec3 transitionStartPos{ 0.0f };
    glm::vec3 transitionEndPos{ 0.0f };
    glm::quat transitionStartOrientation{ 1, 0, 0, 0 };
    glm::quat transitionEndOrientation{ 1, 0, 0, 0 };
    EnterNestedTransition enterNested;

    // View Config
    float previewFadeInDuration = 0.12f;
    float previewFadeOutDuration = 0.18f;
    float previewPreloadRadius = 10.0f;
    int previewPreloadRenderDistance = 2;
    HeadBobSettings headBobSettings{};
    FootstepSettings footstepSettings{};
#pragma endregion

#pragma region 4. Public Core Methods
    // --- 4. Public Core Methods ---

    Player();

    // Main frame update entry point.
    void update(float dt, WorldStack& worldStack,
        PlayerUpdateMode updateMode = PlayerUpdateMode::FullGameplay);

    PersistentState capturePersistentState() const;
    void applyPersistentState(const PersistentState& state);

    void setLifePoints(int value) { lifePoints = glm::clamp(value, 1, kMaxLifePoints); };
#pragma endregion

#pragma region 5. Accessors & State Checks
    // --- 5. Accessors & State Checks ---

    void setAudioController(GameAudioController* controller) { audioController = controller; }

    int getLifePoints() const { return lifePoints; }
    int getMaxLifePoints() const { return kMaxLifePoints; }
    bool isAlive() const { return lifePoints > 0; }
    bool hasSpawnpoint() const { return spawnpointDefined; }
    bool isDeathSequenceActive() const { return deathSequenceActive; }
    float getDeathSequenceElapsedSeconds() const { return deathSequenceElapsedSeconds; }
    const std::string& getDeathSequenceMessage() const { return deathSequenceMessage; }

    float getStandingEyeHeight() const { return standingEyeHeight; }
    float getStandingHeight() const { return standingHeight; }
    bool isGrounded() const { return grounded; }
    bool isCrouching() const { return crouching; }

    bool isSandboxModeEnabled() const { return sandboxModeEnabled; }
    void setSandboxModeEnabled(bool enabled) { sandboxModeEnabled = enabled; }

    double getUniverseCreationCooldownRemainingSeconds() const;

    void applyDamage(int damagePoints);
    glm::vec3 getLifeTextColor() const;
    void setDeathSequenceState(bool active, float elapsedSeconds,
                               const std::string& message = {});
    glm::vec3 getSpawnpointPosition() const { return spawnpointPosition; }
    std::uint32_t getSpawnpointUniverseSeed() const { return spawnpointUniverseSeed; }
    const BiomeSelection& getSpawnpointBiomeSelection() const { return spawnpointBiomeSelection; }
    const std::vector<WorldLevel>& getSpawnpointTraversalStack() const {
        return spawnpointTraversalStack;
    }
    void setSpawnpoint(const glm::vec3& position, std::uint32_t universeSeed,
                       const BiomeSelection& biomeSelection,
                       const std::vector<WorldLevel>& traversalStack = {});

    float getBreakTimeSeconds(BlockType targetType) const {
        return getToolAdjustedBreakTimeSeconds(
            hotbar.getSelectedItem(),
            targetType,
            getBlockHardness(targetType)
        );
    }
#pragma endregion

#pragma region 6. Inventory & Hotbar Integration
    // --- 6. Inventory & Hotbar Integration ---

    // Inventory/hotbar access used by HUD and gameplay.
    const PlayerHotbar& getHotbar() const { return hotbar; }
    int getSelectedHotbarIndex() const { return hotbar.getSelectedIndex(); }
    const InventoryItem& getSelectedHotbarItem() const { return hotbar.getSelectedItem(); }

    BlockType getSelectedHotbarBlockType() const {
        const InventoryItem& selectedItem = hotbar.getSelectedItem();
        return selectedItem.isBlock() ? selectedItem.blockType : BlockType::AIR;
    }

    int getSelectedHotbarCount() const { return hotbar.getSelectedCount(); }

    bool hasSelectedHotbarItem() const {
        return hotbar.hasSelectedItem() && isPlaceableInventoryItem(hotbar.getSelectedItem());
    }

    bool isHoldingTool() const {
        return hotbar.hasSelectedItem() && isToolItem(hotbar.getSelectedItem());
    }

    bool isInventoryOpen() const { return hotbar.isInventoryOpen(); }

    void setInventoryOpen(bool open) {
        const bool changed = hotbar.isInventoryOpen() != open;
        hotbar.setInventoryOpen(open);
        if (changed) {
            notifyInventoryStateChanged();
        }
    }

    void toggleInventoryOpen() {
        const bool wasOpen = hotbar.isInventoryOpen();
        hotbar.toggleInventoryOpen();
        if (hotbar.isInventoryOpen() != wasOpen) {
            notifyInventoryStateChanged();
        }
    }

    bool tryCloseInventory() {
        const bool changed = hotbar.tryCloseInventory();
        if (changed) {
            notifyInventoryStateChanged();
        }
        return changed;
    }

    void closeInventoryForTransition();

    // Raw slot access used by inventory HUD widgets.
    const PlayerHotbar::Slot& getInventorySlot(int index) const {
        return hotbar.getStorageSlot(index);
    }

    const PlayerHotbar::Slot& getExtraInventorySlot(int index) const {
        return hotbar.getExtraSlot(index);
    }

    const PlayerHotbar::Slot& getCraftSlot(int index) const {
        return hotbar.getCraftSlot(index);
    }

    const PlayerHotbar::Slot& getHeldInventorySlot() const {
        return hotbar.getHeldSlot();
    }

    CraftingRecipeResult getCraftingResult() const {
        return hotbar.getCraftingResult();
    }

    CraftingRecipeResult getCraftingBulkResult() const {
        return hotbar.getCraftingBulkResult();
    }

    // Hotbar Actions
    bool clickInventoryStorageSlot(int index, PlayerHotbar::ClickType clickType) {
        return hotbar.clickStorageSlot(index, clickType);
    }

    bool clickCraftSlot(int index, PlayerHotbar::ClickType clickType) {
        return hotbar.clickCraftSlot(index, clickType);
    }

    bool clickCraftResult(PlayerHotbar::ClickType clickType, bool craftAll = false) {
        return hotbar.clickCraftResult(clickType, craftAll);
    }

    bool tryAddItemToInventory(const InventoryItem& item, int amount = 1) {
        return hotbar.addItem(item, amount);
    }

    bool tryAddItemToHotbar(const InventoryItem& item, int amount = 1) {
        return tryAddItemToInventory(item, amount);
    }

    void clearHotbar() { hotbar.clear(); }
#pragma endregion

#pragma region 7. Portals & Nesting Logic
    // --- 7. Portals & Nesting Logic ---

    // Portal preview camera construction is public because rendering/debug code uses it.
    static Camera buildNestedPreviewCamera(const Camera& source,
        const NestedPreviewPortal& portal);
#pragma endregion

private:
#pragma region 8. Internal Constants & State
    // --- 8. Internal Constants & State ---

    static constexpr float kPhysicsMaxStep = 1.0f / 120.0f;
    static constexpr float kSafeFaceDistance = 0.10f;
    static constexpr float kBreakHitRepeatInterval = 0.23f;
    static constexpr float kDroppedItemThrowSpeed = 3.75f;
    static constexpr float kDroppedItemSpawnDistance = 1.55f;
    static constexpr float kDroppedItemPickupDelay = 0.35f;
    static constexpr float kDamageRollDuration = 0.22f;
    static constexpr float kDamageRollAmplitudeDegrees = 14.5f;
    static constexpr float kLifeFlashDuration = 0.30f;
    static constexpr int kLifeFlashPulseCount = 3;
    static constexpr int kMaxLifePoints = 20;
    static constexpr glm::vec3 kLifeTextBaseColor{ 1.0f, 0.42f, 0.42f };

    GameAudioController* audioController = nullptr;
    int lifePoints = kMaxLifePoints;
    bool deathSequenceActive = false;
    float deathSequenceElapsedSeconds = 0.0f;
    std::string deathSequenceMessage{};
    float damageRollTimer = 0.0f;
    float damageRollRadiansCurrent = 0.0f;
    float lifeFlashTimer = 0.0f;
    bool grounded = false;
    bool crouching = false;
    float currentEyeHeight = kDefaultStandingEyeHeight;
    float headBobPhase = 0.0f;
    float headBobBlend = 0.0f;
    glm::vec3 headBobLocalOffset{ 0.0f };
    float headBobRollRadians = 0.0f;
    float lastFootstepPhase = 0.0f;
    bool sandboxModeEnabled = false;
    double nextUniverseCreationTimeSeconds = 0.0;
    bool spawnpointDefined = false;
    glm::vec3 spawnpointPosition{ 0.0f };
    std::uint32_t spawnpointUniverseSeed = 0;
    BiomeSelection spawnpointBiomeSelection{};
    std::vector<WorldLevel> spawnpointTraversalStack{};
#pragma endregion

#pragma region 9. Internal Math & Portals
    // --- 9. Internal Math & Portals ---

    // Portal math helpers.
    static float smoothstep01(float t);
    static void buildPortalBasis(glm::ivec3 faceNormal, glm::vec3& normal,
        glm::vec3& tangent, glm::vec3& bitangent);
    static NestedPreviewFrame buildNestedPreviewFrame(const NestedPreviewPortal& portal);
    static NestedPreviewFrame defaultNestedPreviewFrame();

    static glm::vec3 toPortalLocal(const NestedPreviewFrame& frame, const glm::vec3& worldVec);
    static glm::vec3 fromPortalLocal(const NestedPreviewFrame& frame, const glm::vec3& localVec);

    static NestedPreviewFrame buildPreviewOverrideFrame(const Camera& source,
        const NestedPreviewPortal& portal,
        const Camera& desiredNestedCamera);

    // Portal preview and traversal state.
    void clearNestedPreview();
    void activateNestedPreview(glm::ivec3 block, glm::ivec3 normal, float fadeValue);
    void beginNestedPreviewFadeIn(glm::ivec3 block, glm::ivec3 normal);
    void showNestedPreviewImmediately(glm::ivec3 block, glm::ivec3 normal);

    void updateNestedPreviewAnchorFromSavedState(WorldStack& worldStack,
        glm::ivec3 block, glm::ivec3 normal);
    void setNestedPreviewOverrideFrame(const NestedPreviewFrame& frame);

    bool isLookingAtPortal(FractalWorld* world) const;
    bool canCreateNestedWorldNow() const;
    void markNestedWorldCreated();

    bool tryPrepareNestedWorld(WorldStack& worldStack, const glm::ivec3& blockPos,
        std::uint32_t* outChildSeed = nullptr,
        BiomeSelection* outChildBiome = nullptr,
        std::shared_ptr<const VoxelGame::BiomePreset>* outChildPreset = nullptr);

    void handleTransition(float dt, WorldStack& worldStack);
    void finishDiveIn(WorldStack& worldStack);
    void updateNestedPreview(WorldStack& worldStack, FractalWorld* world, float dt);
    void updatePreviewVisibility(WorldStack& worldStack, bool lookingAtPortal, float dt);

    void preloadNearbyNestedWorld(WorldStack& worldStack, FractalWorld* world, bool lookingAtPortal);
    void enforceSafeNestedSpawn(WorldStack& worldStack, const glm::ivec3& blockPos, Camera& nestedCamera);

    bool isTouchingPortalBlock(glm::ivec3 portalBlock) const;
    bool shouldAutoEnterLookedPortal(FractalWorld* world) const;
    void beginAscendTransition(WorldStack& worldStack);
    void beginNestedEntryTransition(WorldStack& worldStack);
#pragma endregion

#pragma region 10. Internal Systems (Movement, VFX)
    // --- 10. Internal Systems (Movement, VFX) ---

    // Movement and transient state.
    void updateCameraLook();
    void simulateMovement(float dt, FractalWorld* world, bool allowMovementInput);

    void clearTargetSelection();
    void clearTargetOnly();
    void resetBlockBreaking();
    void notifyInventoryStateChanged();

    void triggerDamageFeedback();
    void updateDamageFeedback(float dt);

    void updateHeadBob(float dt, FractalWorld* world, bool active);
    void applyCameraVisualEffects();

    BlockType getFootstepBlockType(FractalWorld* world) const;
    void emitFootstep(FractalWorld* world, float speedAlpha);

    void handleMovement(float dt, bool allowMovementInput);
    void handleHotbarSelectionInput();
    void resolveCollisions(FractalWorld* world, float dt);
    void updateZoom(float dt, bool allowMovementInput);

    float getHorizontalSpeed() const { return glm::length(glm::vec2(velocity.x, velocity.z)); }
    float getCurrentBodyHeight() const;
    float getTargetEyeHeight() const;
    glm::vec3 getFeetPosition() const;
    void setFeetPosition(const glm::vec3& feetPosition);

    bool wantsToCrouch(bool allowMovementInput) const;
    void updateStance(FractalWorld* world, bool allowMovementInput, float dt);

    bool canOccupyBodyAt(FractalWorld* world, const glm::vec3& feetPosition, float bodyHeight) const;
    bool hasSupportBelow(FractalWorld* world, const glm::vec3& feetPosition, float inset, bool requireAllSamples) const;
    void resolveBodyPenetration(FractalWorld* world, glm::vec3& feetPosition, float bodyHeight);
#pragma endregion

#pragma region 11. Block Interaction
    // --- 11. Block Interaction ---

    // Block interaction and targeting.
    void handleBlockInteraction(WorldStack& worldStack, float dt);
    void updateBlockBreaking(WorldStack& worldStack, float dt);
    void breakTargetBlock(WorldStack& worldStack);
    void placeBlockAtTarget(WorldStack& worldStack);
    void dropSelectedItem(WorldStack& worldStack);
    void spawnEnemyAtTarget(WorldStack& worldStack, EnemyType type);
    void doRaycast(FractalWorld* world);
#pragma endregion
};
