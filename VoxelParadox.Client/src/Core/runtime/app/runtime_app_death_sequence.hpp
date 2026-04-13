// runtime_app_death_sequence.hpp
// Death sequence state and flow helpers extracted from runtime_app_loop.cpp.

#pragma once

// 1. Standard Library
#include <cstddef>
#include <string>
#include <vector>

// 2. External Libraries

// 3. Project Headers
#include "player/player.hpp"

class GameAudioController;
class GameChat;
class WorldStack;
class hudPortalInfo;
class hudPortalTracker;
class hudText;

namespace GameplayStatus {
struct PersistentState;
}

namespace RuntimeUI {
struct RuntimeUiState;
}

namespace WorldSaveService {
struct WorldSession;
}

namespace RuntimeAppInternal {

struct DeathSequenceState {
    static constexpr float kDurationSeconds = 20.0f;
    static constexpr float kTextFadeDurationSeconds = 3.0f;
    static constexpr float kVignetteFadeDurationSeconds = 5.0f;

    bool active = false;
    bool paused = false;
    float elapsedSeconds = 0.0f;
    double pausedRenderTimeSeconds = 0.0;

    std::string message{};
    const std::vector<std::string>* messages = nullptr;
    std::size_t messageIndex = 0;
    hudText* messageText = nullptr;
    bool hasPreTeleportPlayerState = false;
    Player::PersistentState preTeleportPlayerState{};
};

float deathScreenTextOpacity(float elapsedSeconds);
float deathScreenVignetteExtra(float elapsedSeconds);
bool deathScreenMusicFadeOutActive(float elapsedSeconds);

void ensureDeathScreenHud(DeathSequenceState& deathState);
void updateDeathScreenMessage(DeathSequenceState& deathState, Player& player);
void updateDeathSequenceFrame(
    DeathSequenceState& deathState,
    Player& player,
    float dt
);
bool deathSequenceFinished(const DeathSequenceState& deathState);

void startDeathSequence(DeathSequenceState& deathState, Player& player,
                        WorldStack& worldStack,
                        GameAudioController& audioController,
                        hudPortalInfo* portalInfo,
                        hudPortalTracker* portalTracker, GameChat& gameChat,
                        RuntimeUI::RuntimeUiState& uiState);

bool finalizeDeathSequence(WorldSaveService::WorldSession& worldSession,
                           Player& player, WorldStack& worldStack,
                           GameAudioController& audioController,
                           GameChat& gameChat,
                           hudPortalTracker* portalTracker,
                           const DeathSequenceState& deathState,
                           RuntimeUI::RuntimeUiState& uiState,
                           const GameplayStatus::PersistentState& gameplayStats,
                           std::string* outError);

void resetDeathSequenceState(DeathSequenceState& deathState, Player& player);

}  // namespace RuntimeAppInternal
