// runtime_app_loop_shared.hpp
// Shared runtime loop helpers used by split runtime modules.

#pragma once

// 1. Standard Library
#include <string>

// 2. External Libraries

// 3. Project Headers
#include "player/player.hpp"

class WorldStack;
class hudPortalTracker;

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

bool saveCurrentWorldSession(
    WorldSaveService::WorldSession& worldSession,
    Player& player,
    WorldStack& worldStack,
    hudPortalTracker* portalTracker,
    RuntimeUI::RuntimeUiState& uiState,
    const GameplayStatus::PersistentState& gameplayStats,
    bool showToast,
    const Player::PersistentState* overriddenPlayerState = nullptr,
    std::string* outError = nullptr
);

}  // namespace RuntimeAppInternal
