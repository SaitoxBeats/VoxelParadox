// File: VoxelParadox.Client/src/Systems/gameplay/portal_interaction_system.hpp
// Purpose: centralizes player portal preview and traversal flow behind a reusable gameplay system.
// Flow: Player forwards nested-world preview/transition entry points here while keeping portal state on the Player facade.

#pragma once

class FractalWorld;
class Player;
class WorldStack;

namespace Gameplay {

class EventQueue;

class PortalInteractionSystem {
public:
    static bool tryCreatePortalForTargetBlock(
        Player& player,
        WorldStack& worldStack,
        EventQueue* eventQueue = nullptr
    );

    static void handleTransition(
        Player& player,
        float dt,
        WorldStack& worldStack
    );

    static void updateNestedPreview(
        Player& player,
        WorldStack& worldStack,
        FractalWorld* world,
        float dt
    );

    static bool beginAscendTransition(
        Player& player,
        WorldStack& worldStack,
        EventQueue* eventQueue = nullptr
    );

    static void beginNestedEntryTransition(
        Player& player,
        WorldStack& worldStack,
        EventQueue* eventQueue = nullptr
    );

private:
    static void finishDiveIn(
        Player& player,
        WorldStack& worldStack
    );

    static void updatePreviewVisibility(
        Player& player,
        WorldStack& worldStack,
        bool lookingAtPortal,
        float dt
    );

    static void preloadNearbyNestedWorld(
        Player& player,
        WorldStack& worldStack,
        FractalWorld* world,
        bool lookingAtPortal
    );
};

}  // namespace Gameplay
