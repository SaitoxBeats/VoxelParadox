// File: VoxelParadox.Client/src/Entities/player/player_experience.hpp
// Purpose: owns the player's experience and level state independently from the wider player controller.
// Flow: Player delegates XP math and persistence shaping to this module, while keeping gameplay side effects outside.

#pragma once

// 1. Standard Library

// 2. Third-party Libraries

// 3. Local Project Modules

class PlayerExperience {
public:
    struct PersistentState {
        float experiencePoints = 0.0f;
        int experienceLevel = 0;
        float experiencePerBlock = kDefaultExperiencePerBlock;
    };

    struct RewardCycleResult {
        int previousLevel = 0;
        int newLevel = 0;
    };

    static constexpr float kMaxExperiencePoints = 100.0f;
    static constexpr float kDefaultExperiencePerBlock = 10.0f;
    static constexpr float kExperiencePerBlockLevelMultiplier = 0.2f;
    static constexpr const char* kRewardItemId = "versal";

    float points() const { return experiencePoints_; }
    float maxPoints() const { return kMaxExperiencePoints; }
    int level() const { return experienceLevel_; }
    float pointsPerBlock() const { return experiencePerBlock_; }

    void setPoints(float value);
    void setLevel(int value);
    void setPointsPerBlock(float value);
    void addPoints(float value);

    bool canConsumeRewardCycle() const;
    RewardCycleResult consumeRewardCycle();

    PersistentState capturePersistentState() const;
    void applyPersistentState(const PersistentState& state);

private:
    float experiencePoints_ = 0.0f;
    int experienceLevel_ = 0;
    float experiencePerBlock_ = kDefaultExperiencePerBlock;
};
