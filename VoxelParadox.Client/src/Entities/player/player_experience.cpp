// File: VoxelParadox.Client/src/Entities/player/player_experience.cpp
// Purpose: implements XP math, reward-cycle thresholds, and persistent experience state sanitation.
// Flow: the player controller delegates XP state changes here and applies rewards around the returned transitions.

#pragma region Includes

// 1. Standard Library
#include <cmath>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "player_experience.hpp"

#pragma endregion

#pragma region 1. Core Modifiers
// --- 1. Core Modifiers ---

void PlayerExperience::setPoints(float value) {
    experiencePoints_ = std::isfinite(value)
        ? glm::clamp(value, 0.0f, kMaxExperiencePoints)
        : 0.0f;
}

void PlayerExperience::setPointsPerBlock(float value) {
    experiencePerBlock_ = std::isfinite(value)
        ? glm::max(0.0f, value)
        : 0.0f;
}

void PlayerExperience::addPoints(float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return;
    }

    experiencePoints_ = glm::max(0.0f, experiencePoints_ + value);
}

#pragma endregion

#pragma region 2. Leveling Logic
// --- 2. Leveling Logic ---

void PlayerExperience::setLevel(int value) {
    const int previousLevel = experienceLevel_;
    experienceLevel_ = glm::max(0, value);

    if (experienceLevel_ > previousLevel) {
        const int gainedLevels = experienceLevel_ - previousLevel;
        const float levelMultiplier = std::pow(
            kExperiencePerBlockLevelMultiplier,
            static_cast<float>(gainedLevels)
        );

        setPointsPerBlock(experiencePerBlock_ * levelMultiplier);
        return;
    }

    if (experienceLevel_ < previousLevel) {
        const int lostLevels = previousLevel - experienceLevel_;
        const float restoreFactor = std::pow(
            kExperiencePerBlockLevelMultiplier,
            static_cast<float>(lostLevels)
        );

        if (restoreFactor > 0.0f) {
            setPointsPerBlock(experiencePerBlock_ / restoreFactor);
        }
        else {
            setPointsPerBlock(0.0f);
        }
    }
}

#pragma endregion

#pragma region 3. Reward Cycle
// --- 3. Reward Cycle ---

bool PlayerExperience::canConsumeRewardCycle() const {
    return experiencePoints_ >= kMaxExperiencePoints;
}

PlayerExperience::RewardCycleResult PlayerExperience::consumeRewardCycle() {
    RewardCycleResult result{};
    result.previousLevel = experienceLevel_;
    result.newLevel = experienceLevel_;

    if (!canConsumeRewardCycle()) {
        return result;
    }

    experiencePoints_ -= kMaxExperiencePoints;
    setLevel(experienceLevel_ + 1);
    result.newLevel = experienceLevel_;

    return result;
}

#pragma endregion

#pragma region 4. Persistent State
// --- 4. Persistent State ---

PlayerExperience::PersistentState PlayerExperience::capturePersistentState() const {
    PersistentState state;
    state.experiencePoints = experiencePoints_;
    state.experienceLevel = experienceLevel_;
    state.experiencePerBlock = experiencePerBlock_;

    return state;
}

void PlayerExperience::applyPersistentState(const PersistentState& state) {
    experiencePoints_ = std::isfinite(state.experiencePoints)
        ? glm::clamp(state.experiencePoints, 0.0f, kMaxExperiencePoints)
        : 0.0f;

    experienceLevel_ = glm::max(0, state.experienceLevel);

    experiencePerBlock_ = std::isfinite(state.experiencePerBlock)
        ? glm::max(0.0f, state.experiencePerBlock)
        : kDefaultExperiencePerBlock;
}

#pragma endregion