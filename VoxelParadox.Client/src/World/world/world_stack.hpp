// WorldStack owns the active world hierarchy, persistence, and traversal.
// Unity analogy:
// - Session manager + save system + dimension/nested-world controller.
// - It decides which world is active, how portal travel works, and how edits are stored.

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "client_assets.hpp"
#include "fractal_world.hpp"
#include "biome.hpp"
#include "biome_registry.hpp"
#include "vox_structure.hpp"

struct WorldEdits {
    std::unordered_map<glm::ivec3, BlockId, IVec3Hash, IVec3Equal> modifiedBlocks;
    std::unordered_map<glm::ivec3, uint32_t, IVec3Hash, IVec3Equal> portalBlocks;
    std::unordered_map<glm::ivec3, BiomeSelection, IVec3Hash, IVec3Equal> portalBiomeSelections;
    std::vector<WorldEnemy> enemies;
    std::string universeName;
    bool hasSavedPlayerState = false;
    glm::vec3 savedPlayerPosition{0.0f};
    glm::quat savedPlayerOrientation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct WorldLevel {
    uint32_t seed;
    BiomeSelection biomeSelection{};
    std::shared_ptr<const VoxelGame::BiomePreset> biomePreset;
    glm::vec3 returnPosition;
    glm::quat returnOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::ivec3 portalBlock;
    glm::ivec3 portalNormal{0};
};

class WorldStack {
public:
    struct NamedPortalEntry {
        glm::ivec3 block{0};
        std::uint32_t childSeed = 0;
        BiomeSelection childBiome{};
        std::string universeName;
    };

    enum class RenderDistancePreset {
        SHORT,
        NORMAL,
        FAR,
        ULTRA,
    };

    // Global save root used by all world persistence.
    static void setSaveWorldDirectory(const std::string& directory);
    static const std::string& getSaveWorldDirectory();

    // Runtime render-distance controls.
    void cycleRenderDistancePreset();
    void stepRenderDistancePreset(int delta);
    void setRenderDistancePreset(RenderDistancePreset preset);
    RenderDistancePreset getRenderDistancePreset() const;
    const char* currentRenderDistanceName() const;
    int configuredRenderDistanceFor(const FractalWorld& world) const;
    void applyRenderDistancePreset(FractalWorld& world) const;

    // Lifecycle.
    void init(uint32_t rootSeed, const BiomeSelection& rootSelection,
              std::shared_ptr<const VoxelGame::BiomePreset> rootPreset = nullptr);
    void shutdown();

    // Active world access.
    FractalWorld* currentWorld() { return activeWorld.get(); }
    const FractalWorld* currentWorld() const { return activeWorld.get(); }
    int currentDepth() const {
        return stack.empty() ? 0 : static_cast<int>(stack.size()) - 1;
    }

    // Traversal and preview management.
    bool descendInto(glm::ivec3 blockPos, glm::vec3 returnPos,
                     const glm::quat& returnOrientation, glm::ivec3 portalNormal);
    bool canAscend() const;
    bool ascend(glm::vec3& outPlayerPos, glm::quat& outPlayerOrientation,
                glm::ivec3& outPortalBlock, glm::ivec3& outPortalNormal);
    void update(const glm::vec3& playerPos, const glm::vec3& viewForward, float dt);
    void updateEnemies(class Player& player, class GameAudioController& audioController,
                       float dt);
    void markAllWorldMeshesDirty();

    template <typename TryCollectFn>
    bool updateDroppedItems(const glm::vec3& playerPos, float dt, TryCollectFn&& tryCollect) {
        if (!activeWorld) {
            return false;
        }
        return activeWorld->updateDroppedItems(
            playerPos, dt, std::forward<TryCollectFn>(tryCollect));
    }

    void render(const glm::vec3& cameraPos, const glm::mat4& viewProjection);
    void render(const glm::vec3& renderCameraPos, const glm::mat4& renderViewProjection,
                const glm::vec3& cullingCameraPos,
                const glm::mat4& cullingViewProjection);

    std::vector<WorldLevel> snapshotTraversalStack() const;
    bool restoreTraversalStack(const std::vector<WorldLevel>& levels);
    void saveCurrentWorldEdits();

    BiomeSelection getResolvedPortalBiomeSelection(const FractalWorld& world,
                                                   const glm::ivec3& blockPos,
                                                   std::uint32_t childSeed) const;
    bool ensureNestedWorldAtBlock(
        const glm::ivec3& blockPos, std::uint32_t* outChildSeed = nullptr,
        BiomeSelection* outChildBiome = nullptr,
        std::shared_ptr<const VoxelGame::BiomePreset>* outChildPreset = nullptr,
        bool* outCreated = nullptr);
    bool hasNestedWorldAtBlock(const glm::ivec3& blockPos) const;
    glm::vec3 resolveCurrentWorldSpawnPosition(const glm::vec3& spawnAnchor,
                                               float playerRadius,
                                               float bodyHeight,
                                               float eyeHeight);
    FractalWorld* getOrCreateNestedPreviewWorld(const glm::ivec3& blockPos);
    void clearNestedPreviewWorld();

    // Metadata and persistence-facing API.
    BiomeSelection currentBiomeSelection() const;
    const std::string& currentBiomeName() const;
    std::string getUniverseName(std::uint32_t seed, const BiomeSelection& biomeSelection);
    void saveActivePlayerState(const glm::vec3& position, const glm::quat& orientation);
    bool tryGetNestedPlayerState(const glm::ivec3& blockPos, glm::vec3& outPosition,
                                 glm::quat& outOrientation);
    void setUniverseName(std::uint32_t seed, const BiomeSelection& biomeSelection,
                         const std::string& name);
    bool deleteUniverseAtPortal(const glm::ivec3& portalBlock);
    std::vector<NamedPortalEntry> listNamedPortalsInCurrentWorld();
    std::uint64_t countNamedUniverses() const;

    // Global cache mirrors serialized world edits.
    std::unordered_map<std::string, WorldEdits> globalCache;

private:
    inline static std::string saveDirectory = ClientAssets::kSaveWorldDirectory;

    struct ResolvedBiomeSelection {
        BiomeSelection selection{};
        std::shared_ptr<const VoxelGame::BiomePreset> preset;
    };

    std::unique_ptr<FractalWorld> activeWorld;
    std::unique_ptr<FractalWorld> previewWorld;
    std::unique_ptr<FractalWorld> returnWorldShell;
    std::vector<WorldLevel> stack;
    std::unordered_map<std::string, glm::vec3> resolvedSpawnCache;
    RenderDistancePreset renderDistancePreset{RenderDistancePreset::NORMAL};
    std::uint32_t previewWorldSeed = 0;
    BiomeSelection previewWorldSelection{};
    std::uint32_t previewParentSeed = 0;

    // Filename and serialization helpers.
    static std::string presetUniverseFilename(std::uint32_t seed,
                                              const std::string& storageId);
    static std::string universeFilename(std::uint32_t seed,
                                        const BiomeSelection& biomeSelection);
    static bool tryParseUniverseSelectionFromFilename(const std::filesystem::path& path,
                                                      std::uint32_t seed,
                                                      BiomeSelection& outSelection);
    static void writeString(std::ofstream& file, const std::string& value);
    static bool readString(std::ifstream& file, std::string& outValue);
    static void writeBiomeSelection(std::ofstream& file,
                                    const BiomeSelection& biomeSelection);
    static bool readBiomeSelection(std::ifstream& file, BiomeSelection& outSelection);

    // Biome selection helpers for portals and defaults.
    static ResolvedBiomeSelection lookupPresetSelection(const BiomeSelection& selection);
    static ResolvedBiomeSelection defaultPresetSelection();
    static ResolvedBiomeSelection pickPortalBiomeSelectionFromSeed(std::uint32_t seed);
    static ResolvedBiomeSelection ensurePortalBiomeSelection(FractalWorld& world,
                                                             const glm::ivec3& portalBlock,
                                                             std::uint32_t childSeed,
                                                             bool persistIfMissing);

    // Recursive cleanup and cache/disk syncing.
    std::uint64_t deleteUniverseRecursive(std::uint32_t seed);
    std::uint64_t deleteUniverseRecursiveInternal(
        std::uint32_t seed,
        std::unordered_set<std::uint32_t>& visited);
    static std::string universeKey(std::uint32_t seed,
                                   const BiomeSelection& biomeSelection);
    glm::vec3 resolveSpawnPositionForWorld(FractalWorld& world,
                                           const glm::vec3& spawnAnchor,
                                           float playerRadius,
                                           float bodyHeight,
                                           float eyeHeight);
    static std::uint32_t hashSeed(std::uint32_t v);
    void saveActiveToCache();
    void applyCacheToActive();
    void applyCacheToWorld(FractalWorld& world);
    void saveToDisk(uint32_t seed, const BiomeSelection& biomeSelection,
                    const WorldEdits& edits);
    bool loadFromDisk(uint32_t seed, const BiomeSelection& biomeSelection,
                      WorldEdits& edits);

    // Child universes are deterministic from parent seed + portal block.
    uint32_t deriveChildSeed(uint32_t parentSeed, glm::ivec3 blockPos);

    // Optional startup content injection for dev flows.
    void injectStartupStructure();
};
