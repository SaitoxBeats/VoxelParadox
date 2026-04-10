#include "player.hpp"

#include <array>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "audio/game_audio_controller.hpp"
#include "engine/engine.hpp"

// Player core:
// - lifetime/setup
// - per-frame orchestration
// - high-level decisions about which subsystems run this frame

namespace {

bool crossedWrappedPhase(float previousPhase, float currentPhase, float targetPhase) {
    if (currentPhase >= previousPhase) {
        return previousPhase < targetPhase && currentPhase >= targetPhase;
    }
    return previousPhase < targetPhase || currentPhase >= targetPhase;
}

}  // namespace

Player::Player() {
    camera.position = glm::vec3(8.0f, 40.0f, 8.0f);
	camera.baseFov = normalFov;
    hotbar.clear();
}

double Player::getUniverseCreationCooldownRemainingSeconds() const {
    if (sandboxModeEnabled) {
        return 0.0;
    }

    return glm::max(0.0, nextUniverseCreationTimeSeconds - ENGINE::GETTIME());
}

void Player::applyDamage(int damagePoints) {
    if (damagePoints <= 0 || !isAlive()) {
        return;
    }

    const int previousLifePoints = lifePoints;
    lifePoints = glm::max(0, lifePoints - damagePoints);
    if (lifePoints < previousLifePoints) {
        triggerDamageFeedback();
        if (audioController) {
            audioController->onPlayerDamaged();
        }
    }
}

glm::vec3 Player::getLifeTextColor() const {
    if (lifeFlashTimer <= 0.0f) {
        return kLifeTextBaseColor;
    }

    const float progress =
        1.0f - (lifeFlashTimer / glm::max(kLifeFlashDuration, 0.0001f));
    const float flashWave = std::sin(
        progress * glm::pi<float>() * static_cast<float>(kLifeFlashPulseCount * 2));
    const float flashIntensity = glm::clamp(flashWave, 0.0f, 1.0f);
    return glm::mix(kLifeTextBaseColor, glm::vec3(1.0f), flashIntensity);
}

Player::PersistentState Player::capturePersistentState() const {
    PersistentState state;
    state.cameraPosition = camera.position;
    state.cameraOrientation = camera.orientation;
    state.velocity = velocity;
    state.lifePoints = lifePoints;
    state.sandboxModeEnabled = sandboxModeEnabled;
    state.universeCreationCooldownRemainingSeconds =
        getUniverseCreationCooldownRemainingSeconds();
    state.doubleJumpCooldownRemainingSeconds =
        glm::max(0.0, nextDoubleJumpTimeSeconds - ENGINE::GETTIME());
    state.hasSpawnpoint = spawnpointDefined;
    state.spawnpointPosition = spawnpointPosition;
    state.spawnpointUniverseSeed = spawnpointUniverseSeed;
    state.spawnpointBiomeSelection = spawnpointBiomeSelection;
    state.spawnpointTraversalStack = spawnpointTraversalStack;
    state.grounded = grounded;
    state.crouching = crouching;
    state.currentEyeHeight = currentEyeHeight;
    state.headBobPhase = headBobPhase;
    state.headBobBlend = headBobBlend;
    state.headBobLocalOffset = headBobLocalOffset;
    state.headBobRollRadians = headBobRollRadians;
    state.lastFootstepPhase = lastFootstepPhase;
    state.damageRollTimer = damageRollTimer;
    state.damageRollRadiansCurrent = damageRollRadiansCurrent;
    state.lifeFlashTimer = lifeFlashTimer;
    state.hotbarState = hotbar.capturePersistentState();
    return state;
}

void Player::applyPersistentState(const PersistentState& state) {
    camera.position = state.cameraPosition;
    camera.orientation = state.cameraOrientation;
    velocity = state.velocity;
    lifePoints = glm::clamp(state.lifePoints, 0, kMaxLifePoints);
    sandboxModeEnabled = state.sandboxModeEnabled;
    nextUniverseCreationTimeSeconds =
        ENGINE::GETTIME() + glm::max(0.0, state.universeCreationCooldownRemainingSeconds);
    nextDoubleJumpTimeSeconds =
        ENGINE::GETTIME() + glm::max(0.0, state.doubleJumpCooldownRemainingSeconds);
    spawnpointDefined = state.hasSpawnpoint;
    spawnpointPosition = state.spawnpointPosition;
    spawnpointUniverseSeed = state.spawnpointUniverseSeed;
    spawnpointBiomeSelection = state.spawnpointBiomeSelection;
    spawnpointTraversalStack = state.spawnpointTraversalStack;
    grounded = state.grounded;
    crouching = state.crouching;
    currentEyeHeight = glm::clamp(state.currentEyeHeight,
                                   kDefaultCrouchingEyeHeight,
                                   kDefaultStandingEyeHeight);
    headBobPhase = state.headBobPhase;
    headBobBlend = glm::clamp(state.headBobBlend, 0.0f, 1.0f);
    headBobLocalOffset = state.headBobLocalOffset;
    headBobRollRadians = state.headBobRollRadians;
    lastFootstepPhase = state.lastFootstepPhase;
    damageRollTimer = glm::max(0.0f, state.damageRollTimer);
    damageRollRadiansCurrent = state.damageRollRadiansCurrent;
    lifeFlashTimer = glm::max(0.0f, state.lifeFlashTimer);
    hotbar.applyPersistentState(state.hotbarState);
    transition = PlayerTransition::NONE;
    transitionTimer = 0.0f;
    clearNestedPreview();
    resetBlockBreaking();
    clearTargetSelection();
    applyCameraVisualEffects();
}

void Player::setDeathSequenceState(bool active, float elapsedSeconds,
                                   const std::string& message) {
    deathSequenceActive = active;
    deathSequenceElapsedSeconds = glm::max(0.0f, elapsedSeconds);
    deathSequenceMessage = active ? message : std::string{};
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

void Player::triggerDamageFeedback() {
    damageRollTimer = kDamageRollDuration;
    lifeFlashTimer = kLifeFlashDuration;
}

void Player::updateDamageFeedback(float dt) {
    damageRollTimer = glm::max(0.0f, damageRollTimer - dt);
    lifeFlashTimer = glm::max(0.0f, lifeFlashTimer - dt);

    if (damageRollTimer <= 0.0f) {
        damageRollRadiansCurrent = 0.0f;
        return;
    }

    const float progress =
        1.0f - (damageRollTimer / glm::max(kDamageRollDuration, 0.0001f));
    const float decay = damageRollTimer / glm::max(kDamageRollDuration, 0.0001f);
    const float oscillation = std::sin(progress * glm::two_pi<float>() * 1.5f);
    damageRollRadiansCurrent =
        oscillation * glm::radians(kDamageRollAmplitudeDegrees) * decay;
}

BlockId Player::getFootstepBlockType(FractalWorld* world) const {
    if (!world) {
        return BlockIds::AIR;
    }

    const glm::vec3 feetPosition = getFeetPosition();
    const float probeY = feetPosition.y - 0.08f;
    const std::array<glm::vec2, 5> samples = {{
        glm::vec2(0.0f, 0.0f),
        glm::vec2(-playerRadius * 0.65f, -playerRadius * 0.65f),
        glm::vec2(-playerRadius * 0.65f, playerRadius * 0.65f),
        glm::vec2(playerRadius * 0.65f, -playerRadius * 0.65f),
        glm::vec2(playerRadius * 0.65f, playerRadius * 0.65f),
    }};

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

void Player::updateHeadBob(float dt, FractalWorld* world, bool active) {
    const bool enabled = headBobSettings.enabled && active && grounded;
    const float horizontalSpeed = getHorizontalSpeed();
    const bool bobActive = enabled && horizontalSpeed > 0.05f;
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

    if (crouching) {
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

void Player::applyCameraVisualEffects() {
    camera.visualLocalOffset = headBobLocalOffset;
    camera.visualRollRadians = damageRollRadiansCurrent + headBobRollRadians;
}

void Player::closeInventoryForTransition() {
    const bool wasOpen = hotbar.isInventoryOpen();
    if (!hotbar.tryCloseInventory()) {
        hotbar.setInventoryOpen(false);
    }
    if (wasOpen && !hotbar.isInventoryOpen()) {
        notifyInventoryStateChanged();
    }
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

void Player::update(float dt, WorldStack& worldStack, PlayerUpdateMode updateMode) {
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

    if (allowGameplayInteractions && isInventoryOpen()) {
        updateHeadBob(dt, nullptr, false);
        applyCameraVisualEffects();
        resetBlockBreaking();
        return;
    }

    if (allowMovementInput) {
        updateCameraLook();
    }

	updateZoom(dt, allowMovementInput);

    FractalWorld* world = worldStack.currentWorld();
    if (allowSimulation) {
        simulateMovement(dt, world, allowMovementInput);
    }

    updateHeadBob(dt, world, allowMovementInput);
    applyCameraVisualEffects();

    if (allowGameplayInteractions && world) {
        doRaycast(world);
    } else if (allowGameplayInteractions) {
        clearTargetSelection();
    }

    if (allowGameplayInteractions) {
        updateNestedPreview(worldStack, world, dt);
        handleBlockInteraction(worldStack, dt);
    } else {
        clearTargetSelection();
        clearNestedPreview();
        resetBlockBreaking();
    }
}
