// File: VoxelParadox.Client/src/Entities/player/player_persistence_system.cpp
// Purpose: implements aggregate player persistence mapping outside the main controller.
// Flow: save services call Player, and Player delegates the detailed state mapping here.

// 1. Third-party Libraries
#include <glm/glm.hpp>

// 2. Local Project Modules
#include "player/player_persistence_system.hpp"

#include "engine/engine.hpp"
#include "player/player_experience.hpp"
#include "player/player_health.hpp"

Player::PersistentState PlayerPersistenceSystem::capture(const Player& player) {
    Player::PersistentState state;
    state.cameraPosition = player.camera.position;
    state.cameraOrientation = player.camera.orientation;
    state.velocity = player.velocity;

    const PlayerHealth::PersistentState healthState =
        player.health.capturePersistentState();
    state.lifePoints = healthState.lifePoints;

    const PlayerExperience::PersistentState experienceState =
        player.experience.capturePersistentState();
    state.experiencePoints = experienceState.experiencePoints;
    state.experienceLevel = experienceState.experienceLevel;
    state.experiencePerBlock = experienceState.experiencePerBlock;

    state.hasOpenedFirstPortal = player.firstPortalOpened;
    state.sandboxModeEnabled = player.sandboxModeEnabled;
    state.universeCreationCooldownRemainingSeconds = 0.0;
    state.doubleJumpCooldownRemainingSeconds =
        glm::max(0.0, player.nextDoubleJumpTimeSeconds - ENGINE::GETTIME());
    state.hasSpawnpoint = player.spawnpointDefined;
    state.spawnpointPosition = player.spawnpointPosition;
    state.spawnpointUniverseSeed = player.spawnpointUniverseSeed;
    state.spawnpointBiomeSelection = player.spawnpointBiomeSelection;
    state.spawnpointTraversalStack = player.spawnpointTraversalStack;
    state.grounded = player.grounded;
    state.crouching = player.crouching;
    state.currentEyeHeight = player.currentEyeHeight;
    state.headBobPhase = player.headBobPhase;
    state.headBobBlend = player.headBobBlend;
    state.headBobLocalOffset = player.headBobLocalOffset;
    state.headBobRollRadians = player.headBobRollRadians;
    state.lastFootstepPhase = player.lastFootstepPhase;
    state.damageRollTimer = healthState.damageRollTimer;
    state.damageRollRadiansCurrent = healthState.damageRollRadiansCurrent;
    state.lifeFlashTimer = healthState.lifeFlashTimer;
    state.hotbarState = player.hotbar.capturePersistentState();
    return state;
}

void PlayerPersistenceSystem::apply(
    Player& player,
    const Player::PersistentState& state
) {
    player.camera.position = state.cameraPosition;
    player.camera.orientation = state.cameraOrientation;
    player.velocity = state.velocity;

    player.health.applyPersistentState({
        state.lifePoints,
        state.damageRollTimer,
        state.damageRollRadiansCurrent,
        state.lifeFlashTimer,
    });
    player.experience.applyPersistentState({
        state.experiencePoints,
        state.experienceLevel,
        state.experiencePerBlock,
    });

    player.firstPortalOpened = state.hasOpenedFirstPortal;
    player.sandboxModeEnabled = state.sandboxModeEnabled;
    player.nextDoubleJumpTimeSeconds =
        ENGINE::GETTIME() + glm::max(0.0, state.doubleJumpCooldownRemainingSeconds);
    player.itemUseCooldownExpiryTimesSeconds.clear();
    player.spawnpointDefined = state.hasSpawnpoint;
    player.spawnpointPosition = state.spawnpointPosition;
    player.spawnpointUniverseSeed = state.spawnpointUniverseSeed;
    player.spawnpointBiomeSelection = state.spawnpointBiomeSelection;
    player.spawnpointTraversalStack = state.spawnpointTraversalStack;
    player.grounded = state.grounded;
    player.crouching = state.crouching;
    player.movementState = PlayerMovementState::Idle;
    player.currentEyeHeight = glm::clamp(
        state.currentEyeHeight,
        Player::kDefaultCrouchingEyeHeight,
        Player::kDefaultStandingEyeHeight
    );
    player.coyoteTimeRemainingSeconds = 0.0f;
    player.jumpBufferRemainingSeconds = 0.0f;
    player.headBobPhase = state.headBobPhase;
    player.headBobBlend = glm::clamp(state.headBobBlend, 0.0f, 1.0f);
    player.headBobLocalOffset = state.headBobLocalOffset;
    player.headBobRollRadians = state.headBobRollRadians;
    player.airborneMaxDownwardSpeed = 0.0f;
    player.landingShakeElapsedSeconds = 0.0f;
    player.landingShakeDurationSeconds = 0.0f;
    player.landingShakeStrength = 0.0f;
    player.landingShakeLocalOffset = glm::vec3(0.0f);
    player.landingShakeRollRadians = 0.0f;
    player.lastFootstepPhase = state.lastFootstepPhase;
    player.headEmbeddedDamageTimer = Player::kHeadEmbeddedDamageIntervalSeconds;
    player.hotbar.applyPersistentState(state.hotbarState);
    player.transition = PlayerTransition::NONE;
    player.transitionTimer = 0.0f;
    player.clearNestedPreview();
    player.resetBlockBreaking();
    player.clearTargetSelection();
    player.updateMovementState();
    player.applyCameraVisualEffects();
}
