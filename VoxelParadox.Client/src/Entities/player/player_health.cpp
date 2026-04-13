// File: VoxelParadox.Client/src/Entities/player/player_health.cpp
// Purpose: implements isolated player life and damage feedback state transitions.
// Flow: gameplay code updates this state every frame and reads the resulting HUD/render values through accessors.

#pragma region Includes

// 1. Standard Library
#include <cmath>

// 2. Third-party Libraries
#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "player_health.hpp"

#pragma endregion

void PlayerHealth::setLifePoints(int value) {
    lifePoints_ = glm::clamp(value, 1, kMaxLifePoints);
}

bool PlayerHealth::applyDamage(int damagePoints) {
    if (damagePoints <= 0 || !isAlive()) {
        return false;
    }

    const int previousLifePoints = lifePoints_;
    lifePoints_ = glm::max(0, lifePoints_ - damagePoints);
    if (lifePoints_ >= previousLifePoints) {
        return false;
    }

    triggerDamageFeedback();
    return true;
}

glm::vec3 PlayerHealth::lifeTextColor() const {
    if (lifeFlashTimer_ <= 0.0f) {
        return kLifeTextBaseColor;
    }

    const float progress =
        1.0f - (lifeFlashTimer_ / glm::max(kLifeFlashDuration, 0.0001f));
    const float flashWave = std::sin(
        progress * glm::pi<float>() * static_cast<float>(kLifeFlashPulseCount * 2)
    );
    const float flashIntensity = glm::clamp(flashWave, 0.0f, 1.0f);
    return glm::mix(kLifeTextBaseColor, glm::vec3(1.0f), flashIntensity);
}

void PlayerHealth::setDeathSequenceState(
    bool active,
    float elapsedSeconds,
    const std::string& message
) {
    deathSequenceActive_ = active;
    deathSequenceElapsedSeconds_ = glm::max(0.0f, elapsedSeconds);
    deathSequenceMessage_ = active ? message : std::string{};
}

void PlayerHealth::updateDamageFeedback(float dt) {
    damageRollTimer_ = glm::max(0.0f, damageRollTimer_ - dt);
    lifeFlashTimer_ = glm::max(0.0f, lifeFlashTimer_ - dt);

    if (damageRollTimer_ <= 0.0f) {
        damageRollRadiansCurrent_ = 0.0f;
        return;
    }

    const float progress =
        1.0f - (damageRollTimer_ / glm::max(kDamageRollDuration, 0.0001f));
    const float decay = damageRollTimer_ / glm::max(kDamageRollDuration, 0.0001f);
    const float oscillation = std::sin(progress * glm::two_pi<float>() * 1.5f);
    damageRollRadiansCurrent_ =
        oscillation * glm::radians(kDamageRollAmplitudeDegrees) * decay;
}

PlayerHealth::PersistentState PlayerHealth::capturePersistentState() const {
    PersistentState state;
    state.lifePoints = lifePoints_;
    state.damageRollTimer = damageRollTimer_;
    state.damageRollRadiansCurrent = damageRollRadiansCurrent_;
    state.lifeFlashTimer = lifeFlashTimer_;
    return state;
}

void PlayerHealth::applyPersistentState(const PersistentState& state) {
    lifePoints_ = glm::clamp(state.lifePoints, 0, kMaxLifePoints);
    damageRollTimer_ = glm::max(0.0f, state.damageRollTimer);
    damageRollRadiansCurrent_ = state.damageRollRadiansCurrent;
    lifeFlashTimer_ = glm::max(0.0f, state.lifeFlashTimer);
}

void PlayerHealth::triggerDamageFeedback() {
    damageRollTimer_ = kDamageRollDuration;
    lifeFlashTimer_ = kLifeFlashDuration;
}
