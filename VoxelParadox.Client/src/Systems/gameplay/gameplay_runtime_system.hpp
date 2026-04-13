// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_runtime_system.hpp
// Purpose: exposes high-level gameplay runtime orchestration outside of the application loop.
// Flow: runtime_app_loop builds a Gameplay::Context, then this system updates gameplay and dispatches events.

#pragma once

class GameChat;
class GameAudioController;
class hudPortalInfo;

namespace GameplayStatus {
class System;
}

namespace Gameplay {

class EventQueue;
struct Context;

class RuntimeSystem {
public:
    static void updateGame(
        Context& gameplayContext,
        hudPortalInfo* portalInfo,
        bool deathSequenceActive,
        bool deathSequencePaused
    );

    static void dispatchEvents(
        EventQueue& eventQueue,
        GameplayStatus::System& gameplayStatus,
        GameChat& gameChat,
        GameAudioController* audioController = nullptr
    );
};

}  // namespace Gameplay
