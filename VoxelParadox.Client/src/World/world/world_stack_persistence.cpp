#include "world_stack.hpp"

#include <algorithm>
#include <cstdio>

#include "gameplay/gameplay_status.hpp"

namespace {

bool tryReadPersistedUniverseName(const std::filesystem::path& path,
                                  std::string& outUniverseName) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    size_t modifiedBlockCount = 0;
    file.read(reinterpret_cast<char*>(&modifiedBlockCount), sizeof(modifiedBlockCount));
    if (!file) {
        return false;
    }

    for (size_t index = 0; index < modifiedBlockCount; index++) {
        glm::ivec3 blockPos{};
        BlockType blockType = BlockType::AIR;
        file.read(reinterpret_cast<char*>(&blockPos), sizeof(blockPos));
        file.read(reinterpret_cast<char*>(&blockType), sizeof(blockType));
        if (!file) {
            return false;
        }
    }

    size_t portalCount = 0;
    file.read(reinterpret_cast<char*>(&portalCount), sizeof(portalCount));
    if (!file) {
        return false;
    }

    for (size_t index = 0; index < portalCount; index++) {
        glm::ivec3 blockPos{};
        std::uint32_t childSeed = 0;
        file.read(reinterpret_cast<char*>(&blockPos), sizeof(blockPos));
        file.read(reinterpret_cast<char*>(&childSeed), sizeof(childSeed));
        if (!file) {
            return false;
        }
    }

    size_t nameSize = 0;
    file.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
    if (!file) {
        return false;
    }

    outUniverseName.clear();
    if (nameSize == 0) {
        return true;
    }

    outUniverseName.resize(nameSize);
    file.read(outUniverseName.data(), static_cast<std::streamsize>(nameSize));
    return static_cast<bool>(file);
}

void writePersistedString(std::ofstream& file, const std::string& value) {
    const size_t size = value.size();
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0) {
        file.write(value.data(), static_cast<std::streamsize>(size));
    }
}

bool readPersistedString(std::ifstream& file, std::string& outValue) {
    size_t size = 0;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!file) {
        return false;
    }

    outValue.clear();
    if (size == 0) {
        return true;
    }

    outValue.resize(size);
    file.read(outValue.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

void writeEnemyModule(std::ofstream& file, const EnemyModule& module) {
    const int moduleType = static_cast<int>(module.type);
    file.write(reinterpret_cast<const char*>(&moduleType), sizeof(moduleType));

    const int touchDamagePoints = module.damagePlayerOnTouch.damagePoints;
    file.write(reinterpret_cast<const char*>(&touchDamagePoints),
               sizeof(touchDamagePoints));
    file.write(reinterpret_cast<const char*>(
                   &module.damagePlayerOnTouch.touchingPlayerLastFrame),
               sizeof(module.damagePlayerOnTouch.touchingPlayerLastFrame));

    writePersistedString(file, module.lookAtPlayerCamera.partName);
    file.write(reinterpret_cast<const char*>(&module.lookAtPlayerCamera.currentYawDegrees),
               sizeof(module.lookAtPlayerCamera.currentYawDegrees));
    file.write(reinterpret_cast<const char*>(&module.lookAtPlayerCamera.currentPitchDegrees),
               sizeof(module.lookAtPlayerCamera.currentPitchDegrees));
    file.write(reinterpret_cast<const char*>(&module.lookAtPlayerCamera.maxPitchDegrees),
               sizeof(module.lookAtPlayerCamera.maxPitchDegrees));
    file.write(reinterpret_cast<const char*>(&module.lookAtPlayerCamera.localYawOffsetDegrees),
               sizeof(module.lookAtPlayerCamera.localYawOffsetDegrees));
    file.write(reinterpret_cast<const char*>(&module.lookAtPlayerCamera.localPitchOffsetDegrees),
               sizeof(module.lookAtPlayerCamera.localPitchOffsetDegrees));

    writePersistedString(file, module.chargePlayerOnLookTrigger.partName);
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.triggerCenterOffset),
               sizeof(module.chargePlayerOnLookTrigger.triggerCenterOffset));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.triggerHalfExtents),
               sizeof(module.chargePlayerOnLookTrigger.triggerHalfExtents));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.triggerSizeMultiplier),
               sizeof(module.chargePlayerOnLookTrigger.triggerSizeMultiplier));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.activationMaxDistance),
               sizeof(module.chargePlayerOnLookTrigger.activationMaxDistance));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.chargeSpeed),
               sizeof(module.chargePlayerOnLookTrigger.chargeSpeed));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.targetYOffset),
               sizeof(module.chargePlayerOnLookTrigger.targetYOffset));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.damagePoints),
               sizeof(module.chargePlayerOnLookTrigger.damagePoints));
    file.write(reinterpret_cast<const char*>(&module.chargePlayerOnLookTrigger.activated),
               sizeof(module.chargePlayerOnLookTrigger.activated));
}

bool readEnemyModule(std::ifstream& file, EnemyModule& outModule) {
    int moduleType = 0;
    file.read(reinterpret_cast<char*>(&moduleType), sizeof(moduleType));
    if (!file) {
        return false;
    }
    outModule.type = static_cast<EnemyModuleType>(moduleType);

    file.read(reinterpret_cast<char*>(&outModule.damagePlayerOnTouch.damagePoints),
              sizeof(outModule.damagePlayerOnTouch.damagePoints));
    file.read(reinterpret_cast<char*>(&outModule.damagePlayerOnTouch.touchingPlayerLastFrame),
              sizeof(outModule.damagePlayerOnTouch.touchingPlayerLastFrame));
    if (!file) {
        return false;
    }

    if (!readPersistedString(file, outModule.lookAtPlayerCamera.partName)) {
        return false;
    }
    file.read(reinterpret_cast<char*>(&outModule.lookAtPlayerCamera.currentYawDegrees),
              sizeof(outModule.lookAtPlayerCamera.currentYawDegrees));
    file.read(reinterpret_cast<char*>(&outModule.lookAtPlayerCamera.currentPitchDegrees),
              sizeof(outModule.lookAtPlayerCamera.currentPitchDegrees));
    file.read(reinterpret_cast<char*>(&outModule.lookAtPlayerCamera.maxPitchDegrees),
              sizeof(outModule.lookAtPlayerCamera.maxPitchDegrees));
    file.read(reinterpret_cast<char*>(&outModule.lookAtPlayerCamera.localYawOffsetDegrees),
              sizeof(outModule.lookAtPlayerCamera.localYawOffsetDegrees));
    file.read(reinterpret_cast<char*>(&outModule.lookAtPlayerCamera.localPitchOffsetDegrees),
              sizeof(outModule.lookAtPlayerCamera.localPitchOffsetDegrees));
    if (!file) {
        return false;
    }

    if (!readPersistedString(file, outModule.chargePlayerOnLookTrigger.partName)) {
        return false;
    }
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.triggerCenterOffset),
              sizeof(outModule.chargePlayerOnLookTrigger.triggerCenterOffset));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.triggerHalfExtents),
              sizeof(outModule.chargePlayerOnLookTrigger.triggerHalfExtents));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.triggerSizeMultiplier),
              sizeof(outModule.chargePlayerOnLookTrigger.triggerSizeMultiplier));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.activationMaxDistance),
              sizeof(outModule.chargePlayerOnLookTrigger.activationMaxDistance));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.chargeSpeed),
              sizeof(outModule.chargePlayerOnLookTrigger.chargeSpeed));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.targetYOffset),
              sizeof(outModule.chargePlayerOnLookTrigger.targetYOffset));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.damagePoints),
              sizeof(outModule.chargePlayerOnLookTrigger.damagePoints));
    file.read(reinterpret_cast<char*>(&outModule.chargePlayerOnLookTrigger.activated),
              sizeof(outModule.chargePlayerOnLookTrigger.activated));
    return static_cast<bool>(file);
}

void writeWorldEnemy(std::ofstream& file, const WorldEnemy& enemy) {
    const int type = static_cast<int>(enemy.type);
    file.write(reinterpret_cast<const char*>(&type), sizeof(type));
    file.write(reinterpret_cast<const char*>(&enemy.position), sizeof(enemy.position));
    file.write(reinterpret_cast<const char*>(&enemy.yawDegrees), sizeof(enemy.yawDegrees));
    file.write(reinterpret_cast<const char*>(&enemy.collider), sizeof(enemy.collider));

    const size_t moduleCount = enemy.modules.size();
    file.write(reinterpret_cast<const char*>(&moduleCount), sizeof(moduleCount));
    for (const EnemyModule& module : enemy.modules) {
        writeEnemyModule(file, module);
    }
}

bool readWorldEnemy(std::ifstream& file, WorldEnemy& outEnemy) {
    int type = 0;
    file.read(reinterpret_cast<char*>(&type), sizeof(type));
    file.read(reinterpret_cast<char*>(&outEnemy.position), sizeof(outEnemy.position));
    file.read(reinterpret_cast<char*>(&outEnemy.yawDegrees), sizeof(outEnemy.yawDegrees));
    file.read(reinterpret_cast<char*>(&outEnemy.collider), sizeof(outEnemy.collider));
    if (!file) {
        return false;
    }

    outEnemy.type = static_cast<EnemyType>(type);
    outEnemy.pendingDestroy = false;

    size_t moduleCount = 0;
    file.read(reinterpret_cast<char*>(&moduleCount), sizeof(moduleCount));
    if (!file) {
        return false;
    }

    outEnemy.modules.clear();
    outEnemy.modules.reserve(moduleCount);
    for (size_t index = 0; index < moduleCount; index++) {
        EnemyModule module{};
        if (!readEnemyModule(file, module)) {
            return false;
        }
        outEnemy.modules.push_back(std::move(module));
    }

    return true;
}

} // namespace

// WorldStack persistence:
// - universe metadata
// - biome selection resolution
// - cache/disk serialization
// - recursive deletion

BiomeSelection WorldStack::currentBiomeSelection() const {
    if (!activeWorld) {
        return defaultPresetSelection().selection;
    }
    return activeWorld->biomeSelection;
}

const std::string& WorldStack::currentBiomeName() const {
    static const std::string kDefaultBiomeName = "No Biome Preset";
    return activeWorld ? activeWorld->biomeSelection.displayName : kDefaultBiomeName;
}

std::string WorldStack::getUniverseName(std::uint32_t seed,
                                        const BiomeSelection& biomeSelection) {
    const std::string key = universeKey(seed, biomeSelection);
    auto it = globalCache.find(key);
    if (it != globalCache.end()) {
        return it->second.universeName;
    }

    WorldEdits edits;
    if (loadFromDisk(seed, biomeSelection, edits)) {
        globalCache[key] = edits;
        return edits.universeName;
    }
    return {};
}

void WorldStack::saveActivePlayerState(const glm::vec3& position,
                                       const glm::quat& orientation) {
    if (!activeWorld) {
        return;
    }

    const std::string key = universeKey(activeWorld->seed, activeWorld->biomeSelection);
    auto it = globalCache.find(key);
    if (it == globalCache.end()) {
        WorldEdits edits;
        loadFromDisk(activeWorld->seed, activeWorld->biomeSelection, edits);
        it = globalCache.emplace(key, std::move(edits)).first;
    }

    it->second.hasSavedPlayerState = true;
    it->second.savedPlayerPosition = position;
    it->second.savedPlayerOrientation = orientation;
    saveToDisk(activeWorld->seed, activeWorld->biomeSelection, it->second);
}

bool WorldStack::tryGetNestedPlayerState(const glm::ivec3& blockPos, glm::vec3& outPosition,
                                         glm::quat& outOrientation) {
    std::uint32_t childSeed = 0;
    BiomeSelection childBiome = defaultPresetSelection().selection;
    if (!ensureNestedWorldAtBlock(blockPos, &childSeed, &childBiome)) {
        return false;
    }

    const std::string key = universeKey(childSeed, childBiome);
    auto it = globalCache.find(key);
    if (it == globalCache.end()) {
        WorldEdits edits;
        if (!loadFromDisk(childSeed, childBiome, edits)) {
            return false;
        }
        it = globalCache.emplace(key, std::move(edits)).first;
    }

    if (!it->second.hasSavedPlayerState) {
        return false;
    }

    outPosition = it->second.savedPlayerPosition;
    outOrientation = it->second.savedPlayerOrientation;
    return true;
}

void WorldStack::setUniverseName(std::uint32_t seed, const BiomeSelection& biomeSelection,
                                 const std::string& name) {
    const std::string key = universeKey(seed, biomeSelection);
    auto it = globalCache.find(key);
    if (it == globalCache.end()) {
        WorldEdits edits;
        loadFromDisk(seed, biomeSelection, edits);
        it = globalCache.emplace(key, std::move(edits)).first;
    }

    it->second.universeName = name;
    saveToDisk(seed, biomeSelection, it->second);
    GameplayStatus::System::instance().setCurrentUniversesCount(
        countNamedUniverses());
}

bool WorldStack::deleteUniverseAtPortal(const glm::ivec3& portalBlock) {
    if (!activeWorld) {
        return false;
    }
    auto it = activeWorld->portalBlocks.find(portalBlock);
    if (it == activeWorld->portalBlocks.end()) {
        return false;
    }

    const uint32_t childSeed = it->second;

    activeWorld->portalBlocks.erase(it);
    activeWorld->portalBiomeSelections.erase(portalBlock);
    activeWorld->markSparseEditIndexDirty();
    activeWorld->setBlock(portalBlock, BlockType::AIR);

    const std::uint64_t deletedUniverseCount = deleteUniverseRecursive(childSeed);
    GameplayStatus::System::instance().recordUniverseDeleted(deletedUniverseCount);
    GameplayStatus::System::instance().setCurrentUniversesCount(
        countNamedUniverses());

    saveActiveToCache();
    return true;
}

std::vector<WorldStack::NamedPortalEntry> WorldStack::listNamedPortalsInCurrentWorld() {
    std::vector<NamedPortalEntry> entries;
    if (!activeWorld) {
        return entries;
    }

    entries.reserve(activeWorld->portalBlocks.size());
    for (const auto& [blockPos, childSeed] : activeWorld->portalBlocks) {
        const BiomeSelection childBiome =
            getResolvedPortalBiomeSelection(*activeWorld, blockPos, childSeed);
        const std::string universeName = getUniverseName(childSeed, childBiome);
        if (universeName.empty()) {
            continue;
        }

        entries.push_back({blockPos, childSeed, childBiome, universeName});
    }

    std::sort(entries.begin(), entries.end(),
              [](const NamedPortalEntry& a, const NamedPortalEntry& b) {
                  if (a.universeName != b.universeName) {
                      return a.universeName < b.universeName;
                  }
                  if (a.block.x != b.block.x) {
                      return a.block.x < b.block.x;
                  }
                  if (a.block.y != b.block.y) {
                      return a.block.y < b.block.y;
                  }
                  return a.block.z < b.block.z;
              });
    return entries;
}

std::uint64_t WorldStack::countNamedUniverses() const {
    std::uint64_t namedUniverseCount = 0;
    std::error_code ec;

    if (!std::filesystem::exists(saveDirectory, ec)) {
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(saveDirectory, ec)) {
        if (ec || !entry.is_regular_file()) {
            ec.clear();
            continue;
        }

        if (entry.path().extension() != ".dat") {
            continue;
        }

        std::string universeName;
        if (!tryReadPersistedUniverseName(entry.path(), universeName)) {
            continue;
        }

        if (!universeName.empty()) {
            ++namedUniverseCount;
        }
    }

    return namedUniverseCount;
}

std::string WorldStack::presetUniverseFilename(std::uint32_t seed,
                                               const std::string& storageId) {
    const std::filesystem::path directory(saveDirectory);
    const std::string filename =
        "world_" + std::to_string(seed) + "_" + storageId + ".dat";
    return (directory / filename).string();
}

std::string WorldStack::universeFilename(std::uint32_t seed,
                                         const BiomeSelection& biomeSelection) {
    return presetUniverseFilename(seed, biomeSelection.storageId);
}

bool WorldStack::tryParseUniverseSelectionFromFilename(const std::filesystem::path& path,
                                                       std::uint32_t seed,
                                                       BiomeSelection& outSelection) {
    const std::string filename = path.filename().string();
    const std::string prefix = "world_" + std::to_string(seed) + "_";
    if (filename.rfind(prefix, 0) != 0 || path.extension() != ".dat") {
        return false;
    }

    const std::string token =
        filename.substr(prefix.size(), filename.size() - prefix.size() - 4);
    constexpr const char* kPresetPrefix = "preset_";
    if (token.rfind(kPresetPrefix, 0) == 0) {
        const std::string presetId = token.substr(7);
        auto preset = BiomeRegistry::instance().findPreset(presetId);
        outSelection = preset ? BiomeSelection::preset(presetId, preset->displayName)
                              : BiomeSelection::preset(presetId, presetId);
        return true;
    }

    return false;
}

void WorldStack::writeString(std::ofstream& file, const std::string& value) {
    const size_t size = value.size();
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0) {
        file.write(value.data(), static_cast<std::streamsize>(size));
    }
}

bool WorldStack::readString(std::ifstream& file, std::string& outValue) {
    size_t size = 0;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!file) {
        return false;
    }

    outValue.clear();
    if (size == 0) {
        return true;
    }

    outValue.resize(size);
    file.read(outValue.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

void WorldStack::writeBiomeSelection(std::ofstream& file,
                                     const BiomeSelection& biomeSelection) {
    const std::uint8_t kind = static_cast<std::uint8_t>(biomeSelection.kind);
    file.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
    writeString(file, biomeSelection.presetId);
    writeString(file, biomeSelection.displayName);
    writeString(file, biomeSelection.storageId);
}

bool WorldStack::readBiomeSelection(std::ifstream& file, BiomeSelection& outSelection) {
    std::uint8_t kind = 0;
    file.read(reinterpret_cast<char*>(&kind), sizeof(kind));
    if (!file) {
        return false;
    }

    outSelection.kind = static_cast<BiomeSelection::Kind>(kind);
    if (!readString(file, outSelection.presetId)) return false;
    if (!readString(file, outSelection.displayName)) return false;
    if (!readString(file, outSelection.storageId)) return false;
    if (outSelection.storageId.empty() && !outSelection.presetId.empty()) {
        outSelection.storageId = "preset_" + outSelection.presetId;
    }
    return outSelection.kind == BiomeSelection::Kind::PRESET &&
           !outSelection.presetId.empty();
}

WorldStack::ResolvedBiomeSelection WorldStack::lookupPresetSelection(
    const BiomeSelection& selection) {
    ResolvedBiomeSelection resolved;
    if (selection.presetId.empty()) {
        return defaultPresetSelection();
    }

    resolved.selection = selection;
    resolved.preset = BiomeRegistry::instance().findPreset(selection.presetId);
    if (!resolved.preset) {
        resolved = defaultPresetSelection();
    } else {
        resolved.selection =
            BiomeSelection::preset(resolved.preset->id, resolved.preset->displayName);
    }
    return resolved;
}

WorldStack::ResolvedBiomeSelection WorldStack::defaultPresetSelection() {
    ResolvedBiomeSelection resolved;
    const auto& presets = BiomeRegistry::instance().presets();
    if (!presets.empty()) {
        resolved.selection = presets.front().selection;
        resolved.preset = presets.front().preset;
        return resolved;
    }

    resolved.selection = BiomeSelection::preset("", "No Biome Preset");
    resolved.selection.storageId.clear();
    return resolved;
}

WorldStack::ResolvedBiomeSelection WorldStack::pickPortalBiomeSelectionFromSeed(
    std::uint32_t seed) {
    const auto& presets = BiomeRegistry::instance().presets();
    if (presets.empty()) {
        return defaultPresetSelection();
    }

    const std::uint32_t choiceHash = hashSeed(seed ^ 0x9E3779B9u);
    const std::size_t presetIndex =
        static_cast<std::size_t>(choiceHash % presets.size());
    ResolvedBiomeSelection resolved;
    resolved.selection = presets[presetIndex].selection;
    resolved.preset = presets[presetIndex].preset;
    return resolved;
}

WorldStack::ResolvedBiomeSelection WorldStack::ensurePortalBiomeSelection(
    FractalWorld& world, const glm::ivec3& portalBlock, std::uint32_t childSeed,
    bool persistIfMissing) {
    auto it = world.portalBiomeSelections.find(portalBlock);
    if (it != world.portalBiomeSelections.end()) {
        return lookupPresetSelection(it->second);
    }

    ResolvedBiomeSelection resolved = pickPortalBiomeSelectionFromSeed(childSeed);
    if (persistIfMissing) {
        world.portalBiomeSelections[portalBlock] = resolved.selection;
        world.markSparseEditIndexDirty();
    }
    return resolved;
}

std::uint64_t WorldStack::deleteUniverseRecursive(std::uint32_t seed) {
    std::unordered_set<std::uint32_t> visited;
    return deleteUniverseRecursiveInternal(seed, visited);
}

std::uint64_t WorldStack::deleteUniverseRecursiveInternal(
    std::uint32_t seed, std::unordered_set<std::uint32_t>& visited) {
    if (visited.find(seed) != visited.end()) {
        return 0;
    }
    visited.insert(seed);

    std::vector<std::uint32_t> children;
    children.reserve(16);

    auto collectChildren = [&](const WorldEdits& edits) {
        for (const auto& kv : edits.portalBlocks) {
            children.push_back(kv.second);
        }
    };

    const std::string keyPrefix = std::to_string(seed) + "|";
    for (const auto& [key, edits] : globalCache) {
        if (key.rfind(keyPrefix, 0) == 0) {
            collectChildren(edits);
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(saveDirectory, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(saveDirectory, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }

            BiomeSelection selection;
            if (!tryParseUniverseSelectionFromFilename(entry.path(), seed, selection)) {
                continue;
            }

            WorldEdits edits;
            if (loadFromDisk(seed, selection, edits)) {
                collectChildren(edits);
            }
        }
    }

    std::uint64_t deletedCount = 1;
    for (std::uint32_t child : children) {
        deletedCount += deleteUniverseRecursiveInternal(child, visited);
    }

    for (auto it = globalCache.begin(); it != globalCache.end();) {
        if (it->first.rfind(std::to_string(seed) + "|", 0) == 0) {
            it = globalCache.erase(it);
        } else {
            ++it;
        }
    }

    ec.clear();
    if (std::filesystem::exists(saveDirectory, ec)) {
        const std::string prefix = "world_" + std::to_string(seed) + "_";
        for (const auto& entry : std::filesystem::directory_iterator(saveDirectory, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix, 0) == 0 && entry.path().extension() == ".dat") {
                std::remove(entry.path().string().c_str());
            }
        }
    }

    return deletedCount;
}

std::string WorldStack::universeKey(std::uint32_t seed,
                                    const BiomeSelection& biomeSelection) {
    return std::to_string(seed) + "|" + biomeSelection.storageId;
}

std::uint32_t WorldStack::hashSeed(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

void WorldStack::saveActiveToCache() {
    // The in-memory cache mirrors disk format so world swaps do not force a reload.
    if (!activeWorld) {
        return;
    }

    const uint32_t seed = activeWorld->seed;
    const BiomeSelection biomeSelection = activeWorld->biomeSelection;
    const std::string key = universeKey(seed, biomeSelection);

    std::string universeName;
    bool hasSavedPlayerState = false;
    glm::vec3 savedPlayerPosition(0.0f);
    glm::quat savedPlayerOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    auto existing = globalCache.find(key);
    if (existing != globalCache.end()) {
        universeName = existing->second.universeName;
        hasSavedPlayerState = existing->second.hasSavedPlayerState;
        savedPlayerPosition = existing->second.savedPlayerPosition;
        savedPlayerOrientation = existing->second.savedPlayerOrientation;
    } else {
        WorldEdits edits;
        if (loadFromDisk(seed, biomeSelection, edits)) {
            universeName = edits.universeName;
            hasSavedPlayerState = edits.hasSavedPlayerState;
            savedPlayerPosition = edits.savedPlayerPosition;
            savedPlayerOrientation = edits.savedPlayerOrientation;
        }
    }

    globalCache[key].modifiedBlocks = activeWorld->modifications;
    globalCache[key].portalBlocks = activeWorld->portalBlocks;
    globalCache[key].portalBiomeSelections = activeWorld->portalBiomeSelections;
    globalCache[key].enemies = activeWorld->enemies;
    globalCache[key].universeName = universeName;
    globalCache[key].hasSavedPlayerState = hasSavedPlayerState;
    globalCache[key].savedPlayerPosition = savedPlayerPosition;
    globalCache[key].savedPlayerOrientation = savedPlayerOrientation;

    saveToDisk(seed, biomeSelection, globalCache[key]);
}

void WorldStack::applyCacheToActive() {
    if (!activeWorld) {
        return;
    }
    applyCacheToWorld(*activeWorld);
}

void WorldStack::applyCacheToWorld(FractalWorld& world) {
    const uint32_t seed = world.seed;
    const BiomeSelection biomeSelection = world.biomeSelection;
    const std::string key = universeKey(seed, biomeSelection);
    auto it = globalCache.find(key);
    if (it != globalCache.end()) {
        world.modifications = it->second.modifiedBlocks;
        world.portalBlocks = it->second.portalBlocks;
        world.portalBiomeSelections = it->second.portalBiomeSelections;
        world.enemies = it->second.enemies;
        world.rebuildSparseEditIndex();
    } else {
        WorldEdits edits;
        if (loadFromDisk(seed, biomeSelection, edits)) {
            globalCache[key] = edits;
            world.modifications = edits.modifiedBlocks;
            world.portalBlocks = edits.portalBlocks;
            world.portalBiomeSelections = edits.portalBiomeSelections;
            world.enemies = edits.enemies;
            world.rebuildSparseEditIndex();
        }
    }
}

void WorldStack::saveToDisk(uint32_t seed, const BiomeSelection& biomeSelection,
                            const WorldEdits& edits) {
    std::string filename = universeFilename(seed, biomeSelection);
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    size_t modSize = edits.modifiedBlocks.size();
    file.write(reinterpret_cast<const char*>(&modSize), sizeof(modSize));
    for (const auto& kv : edits.modifiedBlocks) {
        file.write(reinterpret_cast<const char*>(&kv.first), sizeof(glm::ivec3));
        file.write(reinterpret_cast<const char*>(&kv.second), sizeof(BlockType));
    }

    size_t portalSize = edits.portalBlocks.size();
    file.write(reinterpret_cast<const char*>(&portalSize), sizeof(portalSize));
    for (const auto& kv : edits.portalBlocks) {
        file.write(reinterpret_cast<const char*>(&kv.first), sizeof(glm::ivec3));
        file.write(reinterpret_cast<const char*>(&kv.second), sizeof(uint32_t));
    }

    size_t nameSize = edits.universeName.size();
    file.write(reinterpret_cast<const char*>(&nameSize), sizeof(nameSize));
    if (nameSize > 0) {
        file.write(edits.universeName.data(), static_cast<std::streamsize>(nameSize));
    }

    file.write(reinterpret_cast<const char*>(&edits.hasSavedPlayerState),
               sizeof(edits.hasSavedPlayerState));
    if (edits.hasSavedPlayerState) {
        file.write(reinterpret_cast<const char*>(&edits.savedPlayerPosition),
                   sizeof(edits.savedPlayerPosition));
        file.write(reinterpret_cast<const char*>(&edits.savedPlayerOrientation),
                   sizeof(edits.savedPlayerOrientation));
    }

    const size_t portalSelectionSize = edits.portalBiomeSelections.size();
    file.write(reinterpret_cast<const char*>(&portalSelectionSize),
               sizeof(portalSelectionSize));
    for (const auto& kv : edits.portalBiomeSelections) {
        file.write(reinterpret_cast<const char*>(&kv.first), sizeof(glm::ivec3));
        writeBiomeSelection(file, kv.second);
    }

    const size_t enemyCount = edits.enemies.size();
    file.write(reinterpret_cast<const char*>(&enemyCount), sizeof(enemyCount));
    for (const WorldEnemy& enemy : edits.enemies) {
        writeWorldEnemy(file, enemy);
    }
}

bool WorldStack::loadFromDisk(uint32_t seed, const BiomeSelection& biomeSelection,
                              WorldEdits& edits) {
    // Optional sections at the end keep the binary format backward-compatible.
    std::string filename = universeFilename(seed, biomeSelection);
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    size_t modSize = 0;
    file.read(reinterpret_cast<char*>(&modSize), sizeof(modSize));
    for (size_t i = 0; i < modSize; i++) {
        glm::ivec3 pos;
        BlockType bt;
        file.read(reinterpret_cast<char*>(&pos), sizeof(glm::ivec3));
        file.read(reinterpret_cast<char*>(&bt), sizeof(BlockType));
        edits.modifiedBlocks[pos] = bt;
    }

    size_t portalSize = 0;
    file.read(reinterpret_cast<char*>(&portalSize), sizeof(portalSize));
    for (size_t i = 0; i < portalSize; i++) {
        glm::ivec3 pos;
        uint32_t childSeed;
        file.read(reinterpret_cast<char*>(&pos), sizeof(glm::ivec3));
        file.read(reinterpret_cast<char*>(&childSeed), sizeof(uint32_t));
        edits.portalBlocks[pos] = childSeed;
    }

    std::streampos namePos = file.tellg();
    if (namePos != std::streampos(-1)) {
        file.seekg(0, std::ios::end);
        std::streampos endPos = file.tellg();
        if (endPos != std::streampos(-1) && endPos > namePos) {
            file.seekg(namePos);
            size_t nameSize = 0;
            file.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
            if (file && nameSize > 0) {
                edits.universeName.resize(nameSize);
                file.read(edits.universeName.data(),
                          static_cast<std::streamsize>(nameSize));
                if (!file) {
                    edits.universeName.clear();
                }
            }
        }
    }

    std::streampos statePos = file.tellg();
    if (statePos != std::streampos(-1)) {
        file.seekg(0, std::ios::end);
        std::streampos endPos = file.tellg();
        if (endPos != std::streampos(-1) &&
            endPos - statePos >= static_cast<std::streamoff>(sizeof(bool))) {
            file.seekg(statePos);
            file.read(reinterpret_cast<char*>(&edits.hasSavedPlayerState),
                      sizeof(edits.hasSavedPlayerState));
            if (file && edits.hasSavedPlayerState) {
                file.read(reinterpret_cast<char*>(&edits.savedPlayerPosition),
                          sizeof(edits.savedPlayerPosition));
                file.read(reinterpret_cast<char*>(&edits.savedPlayerOrientation),
                          sizeof(edits.savedPlayerOrientation));
                if (!file) {
                    edits.hasSavedPlayerState = false;
                    edits.savedPlayerPosition = glm::vec3(0.0f);
                    edits.savedPlayerOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }
        }
    }

    std::streampos portalSelectionPos = file.tellg();
    if (portalSelectionPos != std::streampos(-1)) {
        file.seekg(0, std::ios::end);
        const std::streampos endPos = file.tellg();
        if (endPos != std::streampos(-1) &&
            endPos - portalSelectionPos >= static_cast<std::streamoff>(sizeof(size_t))) {
            file.seekg(portalSelectionPos);
            size_t portalSelectionSize = 0;
            file.read(reinterpret_cast<char*>(&portalSelectionSize),
                      sizeof(portalSelectionSize));
            if (file) {
                for (size_t i = 0; i < portalSelectionSize; i++) {
                    glm::ivec3 pos;
                    BiomeSelection selection;
                    file.read(reinterpret_cast<char*>(&pos), sizeof(glm::ivec3));
                    if (!file || !readBiomeSelection(file, selection)) {
                        edits.portalBiomeSelections.clear();
                        return true;
                    }
                    edits.portalBiomeSelections[pos] = selection;
                }
            }
        }
    }

    std::streampos enemySectionPos = file.tellg();
    if (enemySectionPos != std::streampos(-1)) {
        file.seekg(0, std::ios::end);
        const std::streampos endPos = file.tellg();
        if (endPos != std::streampos(-1) &&
            endPos - enemySectionPos >= static_cast<std::streamoff>(sizeof(size_t))) {
            file.seekg(enemySectionPos);
            size_t enemyCount = 0;
            file.read(reinterpret_cast<char*>(&enemyCount), sizeof(enemyCount));
            if (file) {
                edits.enemies.clear();
                edits.enemies.reserve(enemyCount);
                for (size_t index = 0; index < enemyCount; index++) {
                    WorldEnemy enemy{};
                    if (!readWorldEnemy(file, enemy)) {
                        edits.enemies.clear();
                        return true;
                    }
                    edits.enemies.push_back(std::move(enemy));
                }
            }
        }
    }
    return true;
}

uint32_t WorldStack::deriveChildSeed(uint32_t parentSeed, glm::ivec3 blockPos) {
    uint32_t h = parentSeed;
    h ^= static_cast<uint32_t>(blockPos.x) * 374761393u;
    h ^= static_cast<uint32_t>(blockPos.y) * 668265263u;
    h ^= static_cast<uint32_t>(blockPos.z) * 2654435761u;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return h;
}
