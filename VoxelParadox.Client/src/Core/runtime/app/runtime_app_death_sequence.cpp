// runtime_app_death_sequence.cpp
// Death sequence state and flow helpers extracted from runtime_app_loop.cpp.

// 1. Standard Library
#include <utility>
#include <fstream>
#include <filesystem>
#include <random>

// 2. External Libraries

// 3. Project Headers
#include "runtime/app/runtime_app_death_sequence.hpp"

#include "runtime/app/runtime_app_internal.hpp"
#include "runtime/app/runtime_app_loop_shared.hpp"

#include "audio/game_audio_controller.hpp"
#include "client_defaults.hpp"
#include "engine/input.hpp"
#include "path/app_paths.hpp"
#include "gameplay/gameplay_status.hpp"
#include "render/hud/hud.hpp"
#include "render/hud/hud_portal_info.hpp"
#include "render/hud/hud_portal_tracker.hpp"
#include "render/hud/hud_text.hpp"
#include "runtime/state/game_chat.hpp"
#include "runtime/ui/runtime_hud_ids.hpp"
#include "runtime/ui/runtime_ui.hpp"
#include "world/persistence/world_stack.hpp"

namespace {

    std::filesystem::path motivationalMessagesPath() {
        return AppPaths::resolve(ClientAssets::kMotivationalMessagesFile);
    }

    void stripUtf8Bom(std::string& line) {
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
    }

    std::string trimMotivationalLine(std::string line) {
        stripUtf8Bom(line);

        const std::size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return {};
        }

        const std::size_t end = line.find_last_not_of(" \t\r\n");
        return line.substr(begin, end - begin + 1);
    }

    const std::vector<std::string>& defaultMotivationalMessagesFallback() {
        static const std::vector<std::string> messages = {
            "Stand up again. The next step can change everything.",
            "Même la nuit la plus sombre prendra fin et le soleil se lèvera.",
            "Die Hoffnung stirbt zuletzt.",
            "Donde una puerta se cierra, outra se abre.",
            "Dopo il cattivo tempo, viene il bel tempo.",
            "七転び八起き",
            "我们的最大光荣不在于从未跌倒，而在于每次跌倒后都能站起来。",
            "Нет худа без добра.",
            "Per aspera ad astra.",
            "Tudo vale a pena se a alma não é pequena.",
        };
        return messages;
    }

    std::vector<std::string> readMotivationalMessagesFile(
        const std::filesystem::path& path
    ) {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        std::vector<std::string> messages;
        std::string line;

        while (std::getline(file, line)) {
            line = trimMotivationalLine(std::move(line));
            if (!line.empty()) {
                messages.push_back(std::move(line));
            }
        }

        return messages;
    }

    bool writeMotivationalMessagesFile(const std::filesystem::path& path,
                                       const std::vector<std::string>& lines) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        for (std::size_t index = 0; index < lines.size(); ++index) {
            file << lines[index];
            if (index + 1 < lines.size()) {
                file << '\n';
            }
        }

        file << '\n';
        return static_cast<bool>(file);
    }

    std::vector<std::string> loadMotivationalMessages() {
        const std::filesystem::path path = motivationalMessagesPath();
        std::vector<std::string> messages = readMotivationalMessagesFile(path);

        if (!messages.empty()) {
            return messages;
        }

        messages = defaultMotivationalMessagesFallback();
        writeMotivationalMessagesFile(path, messages);
        return messages;
    }

    const std::vector<std::string>& motivationalMessages() {
        static std::vector<std::string> messages;
        messages = loadMotivationalMessages();
        return messages;
    }

    std::size_t randomMotivationalMessageIndex(
        const std::vector<std::string>& messages
    ) {
        if (messages.empty()) {
            return 0;
        }

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> distribution(
            0,
            messages.size() - 1
        );
        return distribution(rng);
    }

    bool sameVec3(const glm::vec3& a, const glm::vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool sameQuat(const glm::quat& a, const glm::quat& b) {
        return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool sameWorldLevel(const WorldLevel& a, const WorldLevel& b) {
        return a.seed == b.seed &&
            a.biomeSelection == b.biomeSelection &&
            sameVec3(a.returnPosition, b.returnPosition) &&
            sameQuat(a.returnOrientation, b.returnOrientation) &&
            a.portalBlock.x == b.portalBlock.x &&
            a.portalBlock.y == b.portalBlock.y &&
            a.portalBlock.z == b.portalBlock.z &&
            a.portalNormal.x == b.portalNormal.x &&
            a.portalNormal.y == b.portalNormal.y &&
            a.portalNormal.z == b.portalNormal.z;
    }

    bool sameTraversalStack(const std::vector<WorldLevel>& a,
                            const std::vector<WorldLevel>& b) {
        if (a.size() != b.size()) {
            return false;
        }

        for (std::size_t index = 0; index < a.size(); ++index) {
            if (!sameWorldLevel(a[index], b[index])) {
                return false;
            }
        }

        return true;
    }

    bool applySpawnWaitingState(Player::PersistentState& state, Player& player,
                                WorldStack& worldStack, bool restoreLifePoints,
                                std::string* outError = nullptr) {
        (void)outError;

        glm::vec3 spawnPosition = worldStack.resolveCurrentWorldSpawnPosition(
            ClientDefaults::kPlayerSpawnPosition,
            Player::kDefaultPlayerRadius,
            Player::kDefaultStandingHeight,
            Player::kDefaultStandingEyeHeight
        );

        if (state.hasSpawnpoint) {
            spawnPosition = state.spawnpointPosition;
        }

        state.cameraPosition = spawnPosition;
        state.velocity = glm::vec3(0.0f);
        state.lifePoints = restoreLifePoints ? player.getMaxLifePoints() : 0;
        state.grounded = false;
        state.crouching = false;
        state.currentEyeHeight = player.getStandingEyeHeight();
        state.headBobBlend = 0.0f;
        state.headBobLocalOffset = glm::vec3(0.0f);
        state.headBobRollRadians = 0.0f;
        state.damageRollTimer = 0.0f;
        state.damageRollRadiansCurrent = 0.0f;
        state.lifeFlashTimer = 0.0f;

        if (!state.hasSpawnpoint && worldStack.currentWorld()) {
            state.hasSpawnpoint = true;
            state.spawnpointPosition = spawnPosition;
            state.spawnpointUniverseSeed = worldStack.currentWorld()->seed;
            state.spawnpointBiomeSelection = worldStack.currentWorld()->biomeSelection;
        }

        return true;
    }

}  // namespace

namespace RuntimeAppInternal {

    float deathScreenTextOpacity(float elapsedSeconds) {
        const float fadeIn = glm::clamp(
            elapsedSeconds / DeathSequenceState::kTextFadeDurationSeconds,
            0.0f,
            1.0f
        );
        const float remaining = DeathSequenceState::kDurationSeconds - elapsedSeconds;
        const float fadeOut = glm::clamp(
            remaining / DeathSequenceState::kTextFadeDurationSeconds,
            0.0f,
            1.0f
        );
        return glm::min(fadeIn, fadeOut);
    }

    float deathScreenVignetteExtra(float elapsedSeconds) {
        if (elapsedSeconds <= DeathSequenceState::kVignetteFadeDurationSeconds) {
            return glm::clamp(
                1.0f - elapsedSeconds / DeathSequenceState::kVignetteFadeDurationSeconds,
                0.0f,
                1.0f
            );
        }

        const float fadeOutStart =
            DeathSequenceState::kDurationSeconds -
            DeathSequenceState::kVignetteFadeDurationSeconds;

        if (elapsedSeconds >= fadeOutStart) {
            return glm::clamp(
                (elapsedSeconds - fadeOutStart) /
                    DeathSequenceState::kVignetteFadeDurationSeconds,
                0.0f,
                1.0f
            );
        }

        return 0.0f;
    }

    bool deathScreenMusicFadeOutActive(float elapsedSeconds) {
        return elapsedSeconds >= (
            DeathSequenceState::kDurationSeconds -
            DeathSequenceState::kVignetteFadeDurationSeconds
        );
    }

    void updateDeathScreenMessage(DeathSequenceState& deathState, Player& player) {
        if (!deathState.messages || deathState.messages->empty()) {
            deathState.message.clear();
            if (deathState.messageText) {
                deathState.messageText->setText("");
                deathState.messageText->setOpacity(
                    deathScreenTextOpacity(deathState.elapsedSeconds)
                );
            }
            player.setDeathSequenceState(
                true,
                deathState.elapsedSeconds,
                deathState.message
            );
            return;
        }

        if (deathState.messageIndex >= deathState.messages->size()) {
            deathState.messageIndex = 0;
        }

        deathState.message = deathState.messages->at(deathState.messageIndex);
        if (deathState.messageText) {
            deathState.messageText->setText(deathState.message);
            deathState.messageText->setOpacity(
                deathScreenTextOpacity(deathState.elapsedSeconds)
            );
        }
        player.setDeathSequenceState(
            true,
            deathState.elapsedSeconds,
            deathState.message
        );
    }

    void ensureDeathScreenHud(DeathSequenceState& deathState) {
        if (deathState.messageText) {
            return;
        }

        auto* text = new hudText(
            "",
            makeHUDLayout(
                HUDAnchor::CENTER,
                HUDAnchor::CENTER,
                glm::vec2(0.0f, -24.0f)
            ),
            glm::vec2(1.15f),
            28,
            HUD::getDefaultFont()
        );

        text->setColor(glm::vec3(0.95f, 0.97f, 1.0f));
        text->setOpacity(0.0f);
        HUD::add(text, RuntimeHudIds::kDeathScreen);
        deathState.messageText = text;
    }

    void startDeathSequence(DeathSequenceState& deathState, Player& player,
                            WorldStack& worldStack,
                            GameAudioController& audioController,
                            hudPortalInfo* portalInfo,
                            hudPortalTracker* portalTracker,
                            GameChat& gameChat,
                            RuntimeUI::RuntimeUiState& uiState) {
        if (deathState.active) {
            return;
        }

        ensureDeathScreenHud(deathState);

        deathState.messages = &motivationalMessages();
        deathState.messageIndex =
            randomMotivationalMessageIndex(*deathState.messages);
        deathState.paused = false;
        deathState.pausedRenderTimeSeconds = 0.0;

        deathState.active = true;
        deathState.elapsedSeconds = 0.0f;
        updateDeathScreenMessage(deathState, player);

        player.tryCloseInventory();
        if (portalInfo) {
            portalInfo->cancelEditing();
        }
        if (portalTracker) {
            portalTracker->closeMenu();
        }
        gameChat.close();

        uiState.settingsMenuOpen = false;
        uiState.settingsDiscardConfirmOpen = false;
        uiState.returnToLauncherRequested = false;
        uiState.controlsCaptureOpen = false;
        uiState.controlsCaptureIgnoreMouseLeft = false;
        uiState.controlsCaptureActionId.clear();
        uiState.controlsWarningMessage.clear();

        Input::setFocusMode(Input::FocusMode::GAMEPLAY);
        Input::setCursorVisible(false);
        HUD::setVisible(false);
        HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, true);

        deathState.preTeleportPlayerState = player.capturePersistentState();
        deathState.hasPreTeleportPlayerState = true;

        Player::PersistentState deathStateData = deathState.preTeleportPlayerState;

        if (!applySpawnWaitingState(deathStateData, player, worldStack, false)) {
            RuntimeAppInternal::printBootstrapError(
                "Failed to move the player to the spawnpoint during death."
            );
            deathStateData.lifePoints = 0;
        }

        player.applyPersistentState(deathStateData);
        player.setDeathSequenceState(
            true,
            deathState.elapsedSeconds,
            deathState.message
        );
        player.setAudioController(&audioController);
        GameplayStatus::System::instance().recordDeath();

        audioController.onDeathSequenceStarted();
    }

    bool finalizeDeathSequence(
        WorldSaveService::WorldSession& worldSession,
        Player& player,
        WorldStack& worldStack,
        GameAudioController& audioController,
        GameChat& gameChat,
        hudPortalTracker* portalTracker,
        const DeathSequenceState& deathState,
        RuntimeUI::RuntimeUiState& uiState,
        const GameplayStatus::PersistentState& gameplayStats,
        std::string* outError
    ) {
        (void)audioController;

        const Player::PersistentState savedUniverseState =
            deathState.hasPreTeleportPlayerState
                ? deathState.preTeleportPlayerState
                : player.capturePersistentState();
        Player::PersistentState respawnState = savedUniverseState;

        worldStack.saveActivePlayerState(
            savedUniverseState.cameraPosition,
            savedUniverseState.cameraOrientation
        );

        const std::vector<WorldLevel>& spawnpointTraversalStack =
            respawnState.spawnpointTraversalStack;

        if (!spawnpointTraversalStack.empty() &&
            !sameTraversalStack(
                worldStack.snapshotTraversalStack(),
                spawnpointTraversalStack
            )) {
            worldStack.saveCurrentWorldEdits();

            if (!worldStack.restoreTraversalStack(spawnpointTraversalStack)) {
                if (outError) {
                    *outError =
                        "Failed to restore the spawn universe during respawn.";
                }
                return false;
            }
        }

        if (!applySpawnWaitingState(
                respawnState,
                player,
                worldStack,
                true,
                outError
            )) {
            return false;
        }

        player.applyPersistentState(respawnState);

        gameChat.close();
        uiState.settingsMenuOpen = false;
        uiState.settingsDiscardConfirmOpen = false;
        uiState.controlsCaptureOpen = false;
        uiState.controlsCaptureIgnoreMouseLeft = false;
        uiState.controlsCaptureActionId.clear();
        uiState.controlsWarningMessage.clear();
        uiState.saveToastTimer = 0.0f;

        HUD::setVisible(true);
        HUD::setGroupEnabled(RuntimeHudIds::kDeathScreen, false);

        if (portalTracker) {
            portalTracker->closeMenu();
        }

        std::string saveError;
        if (!saveCurrentWorldSession(
                worldSession,
                player,
                worldStack,
                portalTracker,
                uiState,
                gameplayStats,
                false,
                nullptr,
                &saveError
            )) {
            if (outError) {
                *outError = saveError;
            }
            return false;
        }

        return true;
    }

    void resetDeathSequenceState(DeathSequenceState& deathState, Player& player) {
        deathState.active = false;
        deathState.paused = false;
        deathState.pausedRenderTimeSeconds = 0.0;
        deathState.elapsedSeconds = 0.0f;
        deathState.message.clear();
        deathState.messages = nullptr;
        deathState.messageIndex = 0;
        deathState.hasPreTeleportPlayerState = false;
        deathState.preTeleportPlayerState = {};

        if (deathState.messageText) {
            deathState.messageText->setText("");
            deathState.messageText->setOpacity(0.0f);
        }

        player.setDeathSequenceState(false, 0.0f);
    }

}  // namespace RuntimeAppInternal
