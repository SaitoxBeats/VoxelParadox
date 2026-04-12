// game_audio_controller.hpp
// Unity mental model: Audio system bridge and event coordinator.
// Maps gameplay events to audio playback requests and manages listener/music state.

#pragma once

#pragma region Includes

// 1. Standard Library
#include <string>
#include <vector>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"
#include "world/block/block.hpp"

#pragma endregion

#pragma region Data Structures
// --- 1. Data Structures ---

struct GameAudioFrameState {
    bool paused = false;
    bool settingsMenuOpen = false;
    bool inventoryOpen = false;
    bool portalPreviewActive = false;
    bool deathScreenActive = false;
    bool deathScreenFadeOutActive = false;
    std::string biomePresetId;
};

#pragma endregion

#pragma region GameAudioController Class
// --- 2. GameAudioController Class ---

class GameAudioController {
public:
    // --- Lifecycle & Core Setup ---
    explicit GameAudioController(ENGINE::AUDIO::AudioManager& audioManager);

    void applySettings(const ENGINE::AUDIO::AudioSettings& settings);

    // Call every frame to update continuous audio state (music, ambiances, 3D listener).
    void syncFrame(const ENGINE::AUDIO::AudioListenerState& listenerState,
        const GameAudioFrameState& frameState, float dtSeconds);

    // --- UI Events ---
    void onHotbarSelectionChanged();
    void onInventoryStateChanged(bool open);
    void onPauseMenuToggled(bool open);

    // --- Gameplay Events (Blocks) ---
    void onBlockHit(BlockId blockType, const glm::ivec3& blockPos, bool initialHit);
    void onBlockBroken(BlockId blockType, const glm::ivec3& blockPos);
    void onBlockPlaced(BlockId blockType, const glm::ivec3& blockPos);

    // --- Gameplay Events (Player/Items) ---
    void onItemCollected();
    void onPlayerDamaged();
    void onPlayerDoubleJump();
    void onPlayerFootstep(BlockId blockType, const glm::vec3& worldPosition,
        float gain, float pitch);
    void onDeathSequenceStarted();
    void playItemEvent(const std::string& eventName);

    // --- Gameplay Events (World/Entities) ---
    void onEnemyTriggerActivated(const glm::vec3& worldPosition);
    void onPortalEntered(const glm::ivec3& blockPos, const std::string& nextBiomePresetId);
    void onPortalExited(const glm::ivec3& blockPos);

private:
    // --- Internal Helpers ---
    ENGINE::AUDIO::SoundPlaybackRequest makeBlockRequest(const glm::ivec3& blockPos,
        bool disableFadeIn = false) const;

    ENGINE::AUDIO::SoundPlaybackRequest makeWorldRequest(const glm::vec3& worldPosition,
        bool disableFadeIn = false) const;

    void playUiEvent(const char* eventName);

    // --- Internal State ---
    ENGINE::AUDIO::AudioManager& audioManager_;
    std::string pendingBiomeOverridePresetId_;

    bool musicBootstrapped_ = false;
    bool hasPendingBiomeOverride_ = false;
    bool forceImmediateMusicRefresh_ = false;
    bool deathScreenActive_ = false;
    bool deathScreenFadeOutStarted_ = false;
};

#pragma endregion
