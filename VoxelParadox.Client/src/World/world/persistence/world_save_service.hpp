// World save service:
// - world folder discovery and creation
// - world.dat / player.dat / stats_player.json persistence
// - shared save-path contract for the Client project

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gameplay/gameplay_status.hpp"
#include "world/biome/biome.hpp"
#include "player/player.hpp"
#include "world/persistence/world_stack.hpp"

class WorldStack;

namespace WorldSaveService {

inline constexpr std::uint32_t kWorldManifestVersion = 1;
inline constexpr std::uint32_t kPlayerDataVersion = 8;

inline constexpr const char* kWorldFileName = "world.dat";
inline constexpr const char* kPlayerDataDirectoryName = "playerdata";
inline constexpr const char* kPlayerDataFileName = "player.dat";
inline constexpr const char* kStatsDirectoryName = "stats";
inline constexpr const char* kStatsFileName = "stats_player.json";
inline constexpr const char* kUniversesDirectoryName = "universes";
inline constexpr const char* kDefaultPlayerName = "player";

struct WorldManifest {
    std::uint32_t schemaVersion = kWorldManifestVersion;
    std::string displayName = "New World";
    std::uint32_t rootSeed = 0;
    BiomeSelection rootBiomeSelection{};
    std::uint32_t activeUniverseSeed = 0;
    BiomeSelection activeUniverseBiomeSelection{};
};

struct WorldPaths {
    std::filesystem::path worldDirectory;
    std::filesystem::path worldFilePath;
    std::filesystem::path universesDirectory;
    std::filesystem::path playerDataDirectory;
    std::filesystem::path playerDataFilePath;
    std::filesystem::path statsDirectory;
    std::filesystem::path statsFilePath;
};

struct PlayerData {
    bool hasPlayerState = false;
    std::string playerName = kDefaultPlayerName;
    Player::PersistentState playerState{};
    std::uint32_t currentUniverseSeed = 0;
    BiomeSelection currentUniverseBiomeSelection{};
    bool hasPortalTrackerState = false;
    struct PortalTrackerState {
        bool trackingActive = false;
        glm::ivec3 trackedBlock{0};
        std::uint32_t trackedWorldSeed = 0;
        BiomeSelection trackedWorldBiome{};
        std::uint32_t trackedChildSeed = 0;
        BiomeSelection trackedChildBiome{};
        std::string trackedUniverseName;
    } portalTrackerState{};
    std::vector<WorldLevel> traversalStack{};
};

struct WorldSummary {
    WorldPaths paths;
    WorldManifest manifest{};
    GameplayStatus::PersistentState gameplayStats{};
    std::filesystem::file_time_type lastWriteTime{};
};

struct WorldSession {
    WorldPaths paths;
    WorldManifest manifest{};
    PlayerData playerData{};
    bool hasPlayerData = false;
    GameplayStatus::PersistentState gameplayStats{};
    std::uint32_t startUniverseSeed = 0;
    BiomeSelection startUniverseBiomeSelection{};
    std::shared_ptr<const VoxelGame::BiomePreset> rootPreset{};
};

std::filesystem::path worldsRoot();
WorldPaths buildWorldPaths(const std::filesystem::path& worldDirectory);
std::string sanitizeDisplayName(const std::string& displayName);
std::string sanitizeFolderName(const std::string& displayName);
std::filesystem::path uniqueWorldDirectoryFor(const std::string& displayName);

std::vector<WorldSummary> listWorlds();
bool createWorld(const std::string& displayName,
                 const BiomeSelection& rootBiomeSelection,
                 WorldSession& outSession,
                 std::string* outError = nullptr);
bool deleteWorld(const std::filesystem::path& worldDirectory,
                 std::string* outError = nullptr);
bool renameWorld(const std::filesystem::path& worldDirectory,
                 const std::string& displayName,
                 std::filesystem::path* outRenamedWorldDirectory = nullptr,
                 std::string* outError = nullptr);
bool loadWorld(const std::filesystem::path& worldDirectory,
               WorldSession& outSession,
               std::string* outError = nullptr);

bool saveWorldManifest(const WorldPaths& paths, const WorldManifest& manifest,
                       std::string* outError = nullptr);
bool loadWorldManifest(const WorldPaths& paths, WorldManifest& outManifest,
                       std::string* outError = nullptr);

bool savePlayerData(const WorldPaths& paths, const PlayerData& playerData,
                    std::string* outError = nullptr);
bool loadPlayerData(const WorldPaths& paths, PlayerData& outPlayerData,
                    std::string* outError = nullptr);

bool saveStats(const WorldPaths& paths,
               const GameplayStatus::PersistentState& gameplayStats,
               std::string* outError = nullptr);
bool loadStats(const WorldPaths& paths, std::uint32_t rootUniverseSeed,
               GameplayStatus::PersistentState& outGameplayStats,
               std::string* outError = nullptr);

bool saveSession(const WorldSession& session, const Player& player,
                 WorldStack& worldStack,
                 const GameplayStatus::PersistentState& gameplayStats,
                 std::string* outError = nullptr);
bool saveSessionWithPlayerState(const WorldSession& session,
                                const Player::PersistentState& playerState,
                                const glm::vec3& cameraPosition,
                                const glm::quat& cameraOrientation,
                                WorldStack& worldStack,
                                const GameplayStatus::PersistentState& gameplayStats,
                                std::string* outError = nullptr);
bool loadPlayerAndWorldSession(const std::filesystem::path& worldDirectory,
                               WorldSession& outSession,
                               std::string* outError = nullptr);

}  // namespace WorldSaveService
