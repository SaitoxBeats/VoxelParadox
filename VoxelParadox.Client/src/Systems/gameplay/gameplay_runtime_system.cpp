// File: VoxelParadox.Client/src/Systems/gameplay/gameplay_runtime_system.cpp
// Purpose: implements high-level gameplay frame orchestration and event dispatch.
// Flow: the app loop stays focused on frame phases while gameplay services update player/world/stat/chat behavior here.

// 1. Standard Library
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// 2. Third-party Libraries

// 3. Local Project Modules
#include "gameplay/gameplay_runtime_system.hpp"

#include "audio/game_audio_controller.hpp"
#include "engine/engine.hpp"
#include "engine/input.hpp"
#include "gameplay/gameplay_context.hpp"
#include "gameplay/gameplay_events.hpp"
#include "gameplay/gameplay_status.hpp"
#include "player/player.hpp"
#include "render/core/renderer.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "runtime/state/game_chat.hpp"
#include "world/persistence/world_stack.hpp"

namespace Gameplay {

void RuntimeSystem::updateGame(
    Context& gameplayContext,
    hudPortalInfo* portalInfo,
    bool deathSequenceActive,
    bool deathSequencePaused
) {
    // --- 1. Validate Runtime State ---
    Player& player = gameplayContext.player;
    WorldStack& worldStack = gameplayContext.worldStack;
    GameAudioController& audioController = *gameplayContext.audioController;
    GameChat& gameChat = *gameplayContext.gameChat;
    hudPortalTracker* portalTracker = gameplayContext.portalTracker;
    const float dt = gameplayContext.dt;

    if (ENGINE::ISPAUSED() || deathSequencePaused) {
        return;
    }

    // --- 2. Resolve Player Update Mode ---
    PlayerUpdateMode playerUpdateMode = PlayerUpdateMode::FullGameplay;

    if (deathSequenceActive ||
        Input::hasUiFocus() ||
        (portalInfo && portalInfo->isEditing()) ||
        (portalTracker && portalTracker->isMenuOpen())) {
        playerUpdateMode = PlayerUpdateMode::Frozen;
    }
    else if (player.isInventoryOpen() || gameChat.isOpen()) {
        playerUpdateMode = PlayerUpdateMode::SimulationOnly;
    }

    // --- 3. Update Gameplay Simulation ---
    player.update(gameplayContext, playerUpdateMode);
    player.updateItemScript(gameplayContext, worldStack.currentWorld(), dt);
    worldStack.update(player.camera.position, player.camera.getForward(), dt);
    worldStack.updateEnemies(player, audioController, dt);

    worldStack.updateDroppedItems(
        player.camera.position,
        dt,
        [&player, &worldStack, &gameplayContext](
            const FractalWorld::DroppedItem& pickedDrop
        ) {
            const InventoryItem& pickedItem = pickedDrop.item;
            if (!player.tryAddItemToInventory(
                    pickedItem,
                    1,
                    gameplayContext.eventQueue
                )) {
                return false;
            }

            gameplayContext.emitItemCollected(pickedItem, 1);
            player.addExperiencePoints(
                pickedDrop.experiencePoints,
                &worldStack,
                gameplayContext.eventQueue
            );
            return true;
        }
    );
}

void RuntimeSystem::dispatchEvents(
    EventQueue& eventQueue,
    GameplayStatus::System& gameplayStatus,
    GameChat& gameChat,
    GameAudioController* audioController,
    Renderer* renderer
) {
    // --- 1. Drain Frame Events ---
    const std::vector<Event> events = eventQueue.drain();

    // --- 2. Apply Shared Gameplay Integrations ---
    for (const Event& event : events) {
        switch (event.type) {
        case EventType::BlockBroken:
            if (event.countStats) {
                gameplayStatus.recordBlocksBroken();
            }
            if (audioController) {
                audioController->onBlockBroken(
                    event.blockType,
                    event.blockPosition
                );
            }
            if (renderer) {
                renderer->emitBlockBreakParticles(
                    event.blockPosition,
                    event.blockType,
                    event.blockNormal
                );
            }
            break;

        case EventType::BlockPlaced:
            gameplayStatus.recordBlocksPlaced();
            if (audioController) {
                audioController->onBlockPlaced(
                    event.blockType,
                    event.blockPosition
                );
            }
            break;

        case EventType::ItemAcquired: {
            if (event.itemAmount <= 0) {
                break;
            }

            const std::uint64_t acquiredCount =
                static_cast<std::uint64_t>(event.itemAmount);

            if (event.item.isBlock()) {
                gameplayStatus.recordBlockAcquired(
                    getBlockId(event.item.blockType),
                    acquiredCount
                );
                break;
            }

            if (event.item.isItem()) {
                const std::uint64_t totalAcquired = gameplayStatus.recordItemAcquired(
                    getItemId(event.item.itemId),
                    acquiredCount
                );
                const std::uint64_t previousAcquired =
                    totalAcquired >= acquiredCount ? totalAcquired - acquiredCount : 0;

                if (previousAcquired == 0 &&
                    totalAcquired > 0 &&
                    std::string(getItemId(event.item.itemId)) == PlayerExperience::kRewardItemId) {
                    gameChat.pushFirstVersalNotification();
                }
            }
            break;
        }

        case EventType::ItemCollected:
            if (audioController) {
                audioController->onItemCollected();
            }
            break;

        case EventType::PlayerExperienceChanged:
            gameplayStatus.setPlayerXp(event.experiencePoints);
            gameplayStatus.setPlayerLevel(event.newLevel);
            if (std::isfinite(event.experienceEarned) &&
                event.experienceEarned > 0.0f) {
                gameplayStatus.recordPlayerXpEarned(event.experienceEarned);
            }
            break;

        case EventType::PlayerDied:
            gameplayStatus.recordDeath();
            break;

        case EventType::PlayerRespawned:
        case EventType::PlayerLevelUp:
        case EventType::PortalCreated:
            break;

        case EventType::UniverseEntered:
            if (audioController) {
                audioController->onPortalEntered(
                    event.blockPosition,
                    event.textValue
                );
            }
            break;

        case EventType::UniverseExited:
            if (audioController) {
                audioController->onPortalExited(event.blockPosition);
            }
            break;
        }
    }
}

}  // namespace Gameplay
