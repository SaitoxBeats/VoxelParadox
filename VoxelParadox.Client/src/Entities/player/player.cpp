#pragma region Includes
#include "player.hpp"

#include <array>
#include <cmath>
#include <cstdio>

#include <glm/gtc/constants.hpp>

#include "audio/game_audio_controller.hpp"
#include "engine/engine.hpp"
#include "gameplay/gameplay_context.hpp"
#include "gameplay/gameplay_events.hpp"
#include "items/item_script_runtime.hpp"
#include "player_health.hpp"
#include "player_interaction_tuning.hpp"
#include "player_persistence_system.hpp"
#include "world/generation/fractal_world.hpp"
#pragma endregion

// Player core:
// - lifetime/setup
// - per-frame orchestration
// - high-level decisions about which subsystems run this frame

// =============================================================================
// --- 1. Internal Utilities ---
// =============================================================================

namespace {

    bool crossedWrappedPhase(float previousPhase, float currentPhase, float targetPhase) {
        if (currentPhase >= previousPhase) {
            return previousPhase < targetPhase && currentPhase >= targetPhase;
        }
        return previousPhase < targetPhase || currentPhase >= targetPhase;
    }

}  // namespace

// =============================================================================
// --- 2. Initialization ---
// =============================================================================

Player::Player() {
    camera.position = glm::vec3(8.0f, 40.0f, 8.0f);
    camera.baseFov = normalFov;
    hotbar.clear();
    updateMovementState();
}

// =============================================================================
// --- 3. Cooldowns ---
// =============================================================================

double Player::getUniverseCreationCooldownRemainingSeconds() const {
    return 0.0;
}

float Player::getItemUseCooldownRemainingSeconds(ItemId itemId) const {
    const auto cooldownIt = itemUseCooldownExpiryTimesSeconds.find(itemId);
    if (cooldownIt == itemUseCooldownExpiryTimesSeconds.end()) {
        return 0.0f;
    }

    return static_cast<float>(glm::max(0.0, cooldownIt->second - ENGINE::GETTIME()));
}

void Player::startItemUseCooldown(ItemId itemId, float cooldownSeconds) {
    if (cooldownSeconds <= 0.0f) {
        itemUseCooldownExpiryTimesSeconds.erase(itemId);
        return;
    }

    itemUseCooldownExpiryTimesSeconds[itemId] =
        ENGINE::GETTIME() + static_cast<double>(cooldownSeconds);
}

// =============================================================================
// --- 4. Inventory ---
// =============================================================================

bool Player::tryAddItemToInventory(
    const InventoryItem& item,
    int amount,
    Gameplay::EventQueue* eventQueue
) {
    if (!hotbar.addItem(item, amount)) {
        return false;
    }

    if (eventQueue) {
        eventQueue->emitItemAcquired(item, amount);
    }

    return true;
}

// =============================================================================
// --- 5. Health & Damage ---
// =============================================================================

void Player::applyDamage(int damagePoints) {
    if (!health.applyDamage(damagePoints)) {
        return;
    }

    if (audioController) {
        audioController->onPlayerDamaged();
    }
}

glm::vec3 Player::getLifeTextColor() const {
    return health.lifeTextColor();
}

void Player::triggerDamageFeedback() {
}

void Player::updateDamageFeedback(float dt) {
    health.updateDamageFeedback(dt);
}

void Player::setDeathSequenceState(bool active, float elapsedSeconds,
    const std::string& message) {
    health.setDeathSequenceState(active, elapsedSeconds, message);
}

// =============================================================================
// --- 6. Experience & Rewards ---
// =============================================================================

void Player::setExperiencePoints(
    float value,
    Gameplay::EventQueue* eventQueue
) {
    experience.setPoints(value);
    if (eventQueue) {
        eventQueue->emitPlayerExperienceChanged(
            experience.points(),
            experience.level()
        );
    }
}

void Player::setExperienceLevel(
    int value,
    Gameplay::EventQueue* eventQueue
) {
    experience.setLevel(value);
    if (eventQueue) {
        eventQueue->emitPlayerExperienceChanged(
            experience.points(),
            experience.level()
        );
    }
}

void Player::setExperiencePerBlock(float value) {
    experience.setPointsPerBlock(value);
}

void Player::addExperiencePoints(
    float value,
    WorldStack* worldStack,
    Gameplay::EventQueue* eventQueue
) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return;
    }

    experience.addPoints(value);

    while (experience.canConsumeRewardCycle()) {
        executeExperienceRewardCommands(worldStack, eventQueue);
    }

    if (eventQueue) {
        eventQueue->emitPlayerExperienceChanged(
            experience.points(),
            experience.level(),
            value
        );
    }
}

void Player::executeExperienceRewardCommands(
    WorldStack* worldStack,
    Gameplay::EventQueue* eventQueue
) {
    const PlayerExperience::RewardCycleResult rewardCycle = experience.consumeRewardCycle();
    if (rewardCycle.newLevel > rewardCycle.previousLevel && eventQueue) {
        eventQueue->emitPlayerLevelUp(rewardCycle.previousLevel, rewardCycle.newLevel);
    }

    grantExperienceRewardItem(worldStack, eventQueue);

    std::printf(
        "[XP] Player reached level %d. Reward granted: %s\n",
        experience.level(),
        PlayerExperience::kRewardItemId
    );
}

void Player::grantExperienceRewardItem(
    WorldStack* worldStack,
    Gameplay::EventQueue* eventQueue
) {
    InventoryItem rewardItem{};
    if (!tryParseInventoryItem(PlayerExperience::kRewardItemId, rewardItem)) {
        return;
    }

    if (tryAddItemToInventory(rewardItem, 1, eventQueue)) {
        return;
    }

    FractalWorld* world = worldStack ? worldStack->currentWorld() : nullptr;
    if (!world) {
        return;
    }

    const glm::vec3 throwDirection = glm::normalize(camera.getForward());
    world->spawnDroppedItemAtPosition(
        camera.position +
        throwDirection * PlayerInteractionTuning::kDroppedItemSpawnDistance,
        rewardItem,
        throwDirection * PlayerInteractionTuning::kDroppedItemThrowSpeed,
        PlayerInteractionTuning::kDroppedItemPickupDelay
    );
}

// =============================================================================
// --- 7. Persistent State ---
// =============================================================================

Player::PersistentState Player::capturePersistentState() const {
    return PlayerPersistenceSystem::capture(*this);
}

void Player::applyPersistentState(const PersistentState& state) {
    PlayerPersistenceSystem::apply(*this, state);
}

void Player::setSpawnpoint(const glm::vec3& position, std::uint32_t universeSeed,
    const BiomeSelection& biomeSelection,
    const std::vector<WorldLevel>& traversalStack) {
    spawnpointDefined = true;
    spawnpointPosition = position;
    spawnpointUniverseSeed = universeSeed;
    spawnpointBiomeSelection = biomeSelection;
    spawnpointTraversalStack = traversalStack;
}

// =============================================================================
// --- 8. Footsteps & Audio ---
// =============================================================================

BlockId Player::getFootstepBlockType(FractalWorld* world) const {
    if (!world) {
        return BlockIds::AIR;
    }

    const glm::vec3 feetPosition = getFeetPosition();
    const float probeY = feetPosition.y - 0.08f;
    const std::array<glm::vec2, 5> samples = { {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(-playerRadius * 0.65f, -playerRadius * 0.65f),
        glm::vec2(-playerRadius * 0.65f, playerRadius * 0.65f),
        glm::vec2(playerRadius * 0.65f, -playerRadius * 0.65f),
        glm::vec2(playerRadius * 0.65f, playerRadius * 0.65f),
    } };

    for (const glm::vec2& sample : samples) {
        const glm::ivec3 blockPos(
            static_cast<int>(std::floor(feetPosition.x + sample.x)),
            static_cast<int>(std::floor(probeY)),
            static_cast<int>(std::floor(feetPosition.z + sample.y)));
        const BlockId blockType = world->getBlock(blockPos);
        if (isSolid(blockType)) {
            return blockType;
        }
    }

    return BlockIds::AIR;
}

void Player::emitFootstep(FractalWorld* world, float speedAlpha) {
    if (!audioController || !world || !footstepSettings.enabled) {
        return;
    }

    const BlockId blockType = getFootstepBlockType(world);
    if (!isSolid(blockType)) {
        return;
    }

    float gain = footstepSettings.baseGain;
    gain *= glm::mix(1.0f, footstepSettings.runGainMultiplier, speedAlpha);
    if (crouching) {
        gain *= footstepSettings.crouchGainMultiplier;
    }

    const float pitchMin = std::min(footstepSettings.pitchMin, footstepSettings.pitchMax);
    const float pitchMax = std::max(footstepSettings.pitchMin, footstepSettings.pitchMax);
    const float pitchAlpha = 0.5f + 0.5f * std::sin(headBobPhase + glm::radians(37.0f));
    const float pitch = glm::mix(pitchMin, pitchMax, pitchAlpha);
    const glm::vec3 worldPosition = getFeetPosition() + glm::vec3(0.0f, 0.15f, 0.0f);
    audioController->onPlayerFootstep(blockType, worldPosition, gain, pitch);
}

// =============================================================================
// --- 9. Head Bob ---
// =============================================================================

void Player::updateHeadBob(float dt, FractalWorld* world, bool active) {
    const bool enabled = headBobSettings.enabled && active && grounded;
    const float horizontalSpeed = getHorizontalSpeed();
    const bool groundedMovementState =
        movementState == PlayerMovementState::Walking ||
        movementState == PlayerMovementState::Running ||
        movementState == PlayerMovementState::Crouching;
    const bool bobActive = enabled && groundedMovementState && horizontalSpeed > 0.05f;
    const float targetBlend = bobActive ? 1.0f : 0.0f;
    const float blendSpeed =
        targetBlend > headBobBlend ? headBobSettings.blendInSpeed
        : headBobSettings.blendOutSpeed;
    const float blendFactor = glm::clamp(blendSpeed * dt, 0.0f, 1.0f);
    headBobBlend = glm::mix(headBobBlend, targetBlend, blendFactor);
    if (headBobBlend <= 0.0001f) {
        headBobBlend = 0.0f;
        headBobLocalOffset = glm::vec3(0.0f);
        headBobRollRadians = 0.0f;
        lastFootstepPhase = headBobPhase;
        return;
    }

    const float runSpeedFloor = glm::max(runSpeed, 0.001f);
    const float speedAlpha = glm::clamp(horizontalSpeed / runSpeedFloor, 0.0f, 1.0f);

    float frequency = glm::mix(headBobSettings.walkFrequency,
        headBobSettings.runFrequency, speedAlpha);
    float verticalAmplitude = glm::mix(headBobSettings.walkVerticalAmplitude,
        headBobSettings.runVerticalAmplitude, speedAlpha);
    float horizontalAmplitude = headBobSettings.horizontalAmplitude;
    float forwardAmplitude = headBobSettings.forwardAmplitude;
    float rollAmplitude = glm::radians(headBobSettings.rollAmplitudeDegrees);

    if (movementState == PlayerMovementState::Crouching) {
        frequency *= headBobSettings.crouchFrequencyMultiplier;
        verticalAmplitude *= headBobSettings.crouchAmplitudeMultiplier;
        horizontalAmplitude *= headBobSettings.crouchAmplitudeMultiplier;
        forwardAmplitude *= headBobSettings.crouchAmplitudeMultiplier;
        rollAmplitude *= headBobSettings.crouchAmplitudeMultiplier;
    }

    const float previousPhase = headBobPhase;
    headBobPhase = std::fmod(headBobPhase + dt * frequency * glm::two_pi<float>(),
        glm::two_pi<float>());
    const float strideWave = std::sin(headBobPhase);
    const float verticalWave = std::abs(strideWave);
    const float swayWave = std::cos(headBobPhase);

    headBobLocalOffset.x = swayWave * horizontalAmplitude * headBobBlend;
    headBobLocalOffset.y =
        ((verticalWave * verticalAmplitude) - (verticalAmplitude * 0.5f)) * headBobBlend;
    headBobLocalOffset.z =
        -verticalWave * forwardAmplitude * headBobBlend;
    headBobRollRadians = strideWave * rollAmplitude * headBobBlend;

    if (!footstepSettings.enabled || !audioController ||
        horizontalSpeed < footstepSettings.minSpeed || headBobBlend < 0.35f) {
        lastFootstepPhase = headBobPhase;
        return;
    }

    const float phaseOffset = glm::radians(footstepSettings.phaseOffsetDegrees);
    const float firstStepPhase =
        std::fmod(glm::half_pi<float>() + phaseOffset + glm::two_pi<float>(),
            glm::two_pi<float>());
    const float secondStepPhase =
        std::fmod(firstStepPhase + glm::pi<float>(), glm::two_pi<float>());
    if (crossedWrappedPhase(previousPhase, headBobPhase, firstStepPhase) ||
        crossedWrappedPhase(previousPhase, headBobPhase, secondStepPhase)) {
        emitFootstep(world, speedAlpha);
    }
    lastFootstepPhase = headBobPhase;
}

// =============================================================================
// --- 10. Camera ---
// =============================================================================

void Player::applyCameraVisualEffects() {
    camera.visualLocalOffset = headBobLocalOffset;
    camera.visualRollRadians = health.damageRollRadiansCurrent() + headBobRollRadians;
}

Camera Player::buildNestedPreviewCamera(const Camera& source,
    const NestedPreviewPortal& portal) {
    // The preview mirrors the player pose through the portal reference frame.
    const NestedPreviewFrame entryFrame = buildNestedPreviewFrame(portal);
    const NestedPreviewFrame nestedFrame =
        portal.hasOverrideFrame ? portal.overrideFrame : defaultNestedPreviewFrame();

    const glm::vec3 relPos = source.position - entryFrame.center;
    const glm::vec3 localPos = toPortalLocal(entryFrame, relPos);
    const glm::vec3 localForward = toPortalLocal(entryFrame, source.getForward());
    const glm::vec3 localUp = toPortalLocal(entryFrame, source.getUp());

    const glm::vec3 mappedPos(-localPos.x, localPos.y, -localPos.z);
    const glm::vec3 mappedForward(-localForward.x, localForward.y, -localForward.z);
    const glm::vec3 mappedUp(-localUp.x, localUp.y, -localUp.z);

    Camera previewCamera = source;
    previewCamera.baseFov = source.baseFov;
    previewCamera.position = nestedFrame.center + fromPortalLocal(nestedFrame, mappedPos);

    const glm::vec3 worldForward =
        glm::normalize(fromPortalLocal(nestedFrame, mappedForward));
    const glm::vec3 worldUp = glm::normalize(fromPortalLocal(nestedFrame, mappedUp));
    previewCamera.lookAt(previewCamera.position + worldForward, worldUp);
    return previewCamera;
}

// =============================================================================
// --- 11. Inventory UI ---
// =============================================================================

void Player::closeInventoryForTransition() {
    const bool wasOpen = hotbar.isInventoryOpen();
    if (!hotbar.tryCloseInventory()) {
        hotbar.setInventoryOpen(false);
    }
    if (wasOpen && !hotbar.isInventoryOpen()) {
        notifyInventoryStateChanged();
    }
}

// =============================================================================
// --- 12. Per-Frame Update ---
// =============================================================================

void Player::update(
    Gameplay::Context& gameplayContext,
    PlayerUpdateMode updateMode
) {
    const float dt = gameplayContext.dt;
    WorldStack& worldStack = gameplayContext.worldStack;

    updateDamageFeedback(dt);

    // Portal transitions own the full frame while they are active.
    if (transition != PlayerTransition::NONE) {
        updateHeadBob(dt, nullptr, false);
        applyCameraVisualEffects();
        closeInventoryForTransition();
        handleTransition(dt, worldStack);
        return;
    }

    if (updateMode == PlayerUpdateMode::Frozen) {
        updateHeadBob(dt, nullptr, false);
        applyCameraVisualEffects();
        resetBlockBreaking();
        return;
    }

    const bool allowGameplayInteractions =
        updateMode == PlayerUpdateMode::FullGameplay;
    const bool allowMovementInput =
        updateMode == PlayerUpdateMode::FullGameplay;
    const bool allowSimulation = updateMode != PlayerUpdateMode::Frozen;

    if (allowGameplayInteractions) {
        handleHotbarSelectionInput();
    }
    if (allowGameplayInteractions && Input::keyPressed(GLFW_KEY_Q)) {
        dropSelectedItem(worldStack);
    }

    FractalWorld* world = worldStack.currentWorld();

    if (allowGameplayInteractions && isInventoryOpen()) {
        updateEmbeddedHeadDamage(world, dt);
        updateHeadBob(dt, nullptr, false);
        applyCameraVisualEffects();
        resetBlockBreaking();
        return;
    }

    if (allowMovementInput) {
        updateCameraLook();
    }

    updateZoom(dt, allowMovementInput);

    if (allowSimulation) {
        simulateMovement(dt, world, allowMovementInput);
    }

    updateEmbeddedHeadDamage(world, dt);

    updateHeadBob(dt, world, allowMovementInput);
    applyCameraVisualEffects();

    if (allowGameplayInteractions && world) {
        doRaycast(world);
    }
    else if (allowGameplayInteractions) {
        clearTargetSelection();
    }

    if (allowGameplayInteractions) {
        updateNestedPreview(worldStack, world, dt);
        handleBlockInteraction(gameplayContext);
    }
    else {
        clearTargetSelection();
        clearNestedPreview();
        resetBlockBreaking();
    }
}
