// File: VoxelParadox.Client/src/Entities/player/player_health.hpp
// Purpose: owns player life, damage feedback, and death-sequence state separately from movement and interaction.
// Flow: Player delegates health state updates here and keeps only the surrounding runtime integration.

#pragma once

// 1. Standard Library
#include <string>

// 2. Third-party Libraries
#include <glm/glm.hpp>

class PlayerHealth {
public:
    struct PersistentState {
        int lifePoints = kMaxLifePoints;
        float damageRollTimer = 0.0f;
        float damageRollRadiansCurrent = 0.0f;
        float lifeFlashTimer = 0.0f;
    };

    static constexpr float kDamageRollDuration = 0.22f;
    static constexpr float kDamageRollAmplitudeDegrees = 10.5f;
    static constexpr float kLifeFlashDuration = 0.30f;
    static constexpr int kLifeFlashPulseCount = 3;
    static constexpr int kMaxLifePoints = 20;
    static constexpr glm::vec3 kLifeTextBaseColor{ 0.0f, 0.0f, 0.0f };

    void setLifePoints(int value);
    bool applyDamage(int damagePoints);

    int lifePoints() const { return lifePoints_; }
    int maxLifePoints() const { return kMaxLifePoints; }
    bool isAlive() const { return lifePoints_ > 0; }

    glm::vec3 lifeTextColor() const;
    void setDeathSequenceState(
        bool active,
        float elapsedSeconds,
        const std::string& message = {}
    );
    bool isDeathSequenceActive() const { return deathSequenceActive_; }
    float deathSequenceElapsedSeconds() const { return deathSequenceElapsedSeconds_; }
    const std::string& deathSequenceMessage() const { return deathSequenceMessage_; }

    void updateDamageFeedback(float dt);
    float damageRollRadiansCurrent() const { return damageRollRadiansCurrent_; }

    PersistentState capturePersistentState() const;
    void applyPersistentState(const PersistentState& state);

private:
    void triggerDamageFeedback();

    int lifePoints_ = kMaxLifePoints;
    bool deathSequenceActive_ = false;
    float deathSequenceElapsedSeconds_ = 0.0f;
    std::string deathSequenceMessage_{};
    float damageRollTimer_ = 0.0f;
    float damageRollRadiansCurrent_ = 0.0f;
    float lifeFlashTimer_ = 0.0f;
};
