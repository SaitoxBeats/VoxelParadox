#include "world/persistence/world_save_service.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "path/app_paths.hpp"
#include "items/item_catalog.hpp"
#include "world/block/block_catalog.hpp"

namespace WorldSaveService {
namespace {

using json = nlohmann::json;

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool isInvalidFolderChar(unsigned char ch) {
    switch (ch) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
        return true;
    default:
        return ch < 32;
    }
}

bool isSubdirectoryOf(const std::filesystem::path& root,
                      const std::filesystem::path& target) {
    const std::string rootString = root.string();
    std::string rootPrefix = rootString;
    if (!rootPrefix.empty() &&
        rootPrefix.back() != std::filesystem::path::preferred_separator) {
        rootPrefix.push_back(std::filesystem::path::preferred_separator);
    }

    const std::string targetString = target.string();
    return targetString.rfind(rootPrefix, 0) == 0;
}

bool resolveWorldDirectoryPath(const std::filesystem::path& worldDirectory,
                              std::filesystem::path& outRoot,
                              std::filesystem::path& outTarget,
                              std::string* outError) {
    std::error_code ec;

    outRoot = std::filesystem::weakly_canonical(worldsRoot(), ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to resolve worlds root directory.";
        }
        return false;
    }

    outTarget = std::filesystem::weakly_canonical(worldDirectory, ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to resolve world directory: " + worldDirectory.string();
        }
        return false;
    }

    if (!isSubdirectoryOf(outRoot, outTarget)) {
        if (outError) {
            *outError = "Refusing to modify a directory outside the worlds root.";
        }
        return false;
    }

    return true;
}

std::filesystem::path uniqueWorldDirectoryForRename(
    const std::filesystem::path& sourceDirectory,
    const std::string& displayName) {
    const std::string sanitized = sanitizeFolderName(displayName);
    const std::filesystem::path root = worldsRoot();
    const std::filesystem::path desired = root / sanitized;

    std::error_code ec;
    if (!std::filesystem::exists(desired, ec)) {
        return desired;
    }

    if (!ec && std::filesystem::equivalent(sourceDirectory, desired, ec)) {
        return desired;
    }
    ec.clear();

    for (std::uint32_t suffix = 2; suffix < 10000; ++suffix) {
        const std::filesystem::path candidate =
            root / (sanitized + " (" + std::to_string(suffix) + ")");

        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }

        if (!ec && std::filesystem::equivalent(sourceDirectory, candidate, ec)) {
            return candidate;
        }
    }

    return uniqueWorldDirectoryFor(displayName);
}

}  // namespace

json serializeWorldLevel(const WorldLevel& level);
bool deserializeWorldLevel(const json& value, WorldLevel& outLevel);

std::shared_ptr<const VoxelGame::BiomePreset> resolvePreset(
    const BiomeSelection& selection) {
    if (!selection.presetId.empty()) {
        if (const auto preset = BiomeRegistry::instance().findPreset(selection.presetId)) {
            return preset;
        }
    }

    const auto& presets = BiomeRegistry::instance().presets();
    if (!presets.empty()) {
        return presets.front().preset;
    }

    return {};
}

json serializeVec3(const glm::vec3& value) {
    return json{
        {"x", value.x},
        {"y", value.y},
        {"z", value.z},
    };
}

bool deserializeVec3(const json& value, glm::vec3& outValue) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("x") && value["x"].is_number()) {
        outValue.x = value["x"].get<float>();
    }
    if (value.contains("y") && value["y"].is_number()) {
        outValue.y = value["y"].get<float>();
    }
    if (value.contains("z") && value["z"].is_number()) {
        outValue.z = value["z"].get<float>();
    }
    return true;
}

json serializeQuat(const glm::quat& value) {
    return json{
        {"w", value.w},
        {"x", value.x},
        {"y", value.y},
        {"z", value.z},
    };
}

bool deserializeQuat(const json& value, glm::quat& outValue) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("w") && value["w"].is_number()) {
        outValue.w = value["w"].get<float>();
    }
    if (value.contains("x") && value["x"].is_number()) {
        outValue.x = value["x"].get<float>();
    }
    if (value.contains("y") && value["y"].is_number()) {
        outValue.y = value["y"].get<float>();
    }
    if (value.contains("z") && value["z"].is_number()) {
        outValue.z = value["z"].get<float>();
    }
    return true;
}

json serializeBiomeSelection(const BiomeSelection& selection) {
    return json{
        {"kind", "preset"},
        {"presetId", selection.presetId},
        {"displayName", selection.displayName},
        {"storageId", selection.storageId},
    };
}

bool deserializeBiomeSelection(const json& value, BiomeSelection& outSelection) {
    if (!value.is_object()) {
        return false;
    }

    std::string presetId;
    std::string displayName;
    std::string storageId;
    if (value.contains("presetId") && value["presetId"].is_string()) {
        presetId = value["presetId"].get<std::string>();
    }
    if (value.contains("displayName") && value["displayName"].is_string()) {
        displayName = value["displayName"].get<std::string>();
    }
    if (value.contains("storageId") && value["storageId"].is_string()) {
        storageId = value["storageId"].get<std::string>();
    }

    if (presetId.empty()) {
        return false;
    }

    outSelection = BiomeSelection::preset(presetId, displayName.empty() ? presetId : displayName);
    if (!storageId.empty()) {
        outSelection.storageId = storageId;
    }
    return true;
}

json serializeInventoryItem(const InventoryItem& item) {
    return json{
        {"id", getInventoryItemId(item)},
    };
}

bool deserializeInventoryItem(const json& value, InventoryItem& outItem) {
    if (value.is_string()) {
        return tryParseInventoryItem(value.get<std::string>(), outItem);
    }

    if (!value.is_object()) {
        outItem = {};
        return false;
    }

    if (value.contains("id") && value["id"].is_string()) {
        return tryParseInventoryItem(value["id"].get<std::string>(), outItem);
    }

    outItem = {};
    return false;
}

json serializeSlot(const PlayerHotbar::Slot& slot) {
    return json{
        {"item", serializeInventoryItem(slot.item)},
        {"count", slot.count},
    };
}

bool deserializeSlot(const json& value, PlayerHotbar::Slot& outSlot) {
    if (!value.is_object()) {
        outSlot = {};
        return false;
    }

    PlayerHotbar::Slot slot{};
    if (value.contains("item")) {
        deserializeInventoryItem(value["item"], slot.item);
    }
    if (value.contains("count") && value["count"].is_number_integer()) {
        slot.count = value["count"].get<int>();
    }
    outSlot = slot;
    return true;
}

json serializeHotbarState(const PlayerHotbar::PersistentState& state) {
    json storageSlots = json::array();
    for (const auto& slot : state.storageSlots) {
        storageSlots.push_back(serializeSlot(slot));
    }

    json craftSlots = json::array();
    for (const auto& slot : state.craftSlots) {
        craftSlots.push_back(serializeSlot(slot));
    }

    return json{
        {"storageSlots", storageSlots},
        {"craftSlots", craftSlots},
        {"heldSlot", serializeSlot(state.heldSlot)},
        {"selectedIndex", state.selectedIndex},
        {"inventoryOpen", state.inventoryOpen},
    };
}

bool deserializeHotbarState(const json& value, PlayerHotbar::PersistentState& outState) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("storageSlots") && value["storageSlots"].is_array()) {
        const auto& storage = value["storageSlots"];
        const int limit = std::min(
            static_cast<int>(storage.size()), PlayerHotbar::TOTAL_STORAGE_SLOTS);
        for (int index = 0; index < limit; ++index) {
            deserializeSlot(storage[static_cast<std::size_t>(index)],
                            outState.storageSlots[static_cast<std::size_t>(index)]);
        }
    }

    if (value.contains("craftSlots") && value["craftSlots"].is_array()) {
        const auto& craft = value["craftSlots"];
        const int limit = std::min(
            static_cast<int>(craft.size()), PlayerHotbar::CRAFT_SLOT_COUNT);
        for (int index = 0; index < limit; ++index) {
            deserializeSlot(craft[static_cast<std::size_t>(index)],
                            outState.craftSlots[static_cast<std::size_t>(index)]);
        }
    }

    if (value.contains("heldSlot")) {
        deserializeSlot(value["heldSlot"], outState.heldSlot);
    }
    if (value.contains("selectedIndex") && value["selectedIndex"].is_number_integer()) {
        outState.selectedIndex = value["selectedIndex"].get<int>();
    }
    if (value.contains("inventoryOpen") && value["inventoryOpen"].is_boolean()) {
        outState.inventoryOpen = value["inventoryOpen"].get<bool>();
    }

    return true;
}

json serializePlayerState(const Player::PersistentState& state) {
    json spawnpointTraversalStack = json::array();
    for (const WorldLevel& level : state.spawnpointTraversalStack) {
        spawnpointTraversalStack.push_back(serializeWorldLevel(level));
    }

    return json{
        {"cameraPosition", serializeVec3(state.cameraPosition)},
        {"cameraOrientation", serializeQuat(state.cameraOrientation)},
        {"velocity", serializeVec3(state.velocity)},
        {"lifePoints", state.lifePoints},
        {"experiencePoints", state.experiencePoints},
        {"experienceLevel", state.experienceLevel},
        {"experiencePerBlock", state.experiencePerBlock},
        {"sandboxModeEnabled", state.sandboxModeEnabled},
        {"universeCreationCooldownRemainingSeconds",
         state.universeCreationCooldownRemainingSeconds},
        {"doubleJumpCooldownRemainingSeconds",
         state.doubleJumpCooldownRemainingSeconds},
        {"hasSpawnpoint", state.hasSpawnpoint},
        {"spawnpointPosition", serializeVec3(state.spawnpointPosition)},
        {"spawnpointUniverseSeed", state.spawnpointUniverseSeed},
        {"spawnpointBiomeSelection",
         serializeBiomeSelection(state.spawnpointBiomeSelection)},
        {"spawnpointTraversalStack", spawnpointTraversalStack},
        {"grounded", state.grounded},
        {"crouching", state.crouching},
        {"currentEyeHeight", state.currentEyeHeight},
        {"headBobPhase", state.headBobPhase},
        {"headBobBlend", state.headBobBlend},
        {"headBobLocalOffset", serializeVec3(state.headBobLocalOffset)},
        {"headBobRollRadians", state.headBobRollRadians},
        {"lastFootstepPhase", state.lastFootstepPhase},
        {"damageRollTimer", state.damageRollTimer},
        {"damageRollRadiansCurrent", state.damageRollRadiansCurrent},
        {"lifeFlashTimer", state.lifeFlashTimer},
        {"hotbarState", serializeHotbarState(state.hotbarState)},
    };
}

int defaultPlayerLifePoints() {
    static const int kDefaultLifePoints = Player().getMaxLifePoints();
    return kDefaultLifePoints;
}

void sanitizeDeathState(Player::PersistentState& state) {
    if (state.lifePoints > 0) {
        return;
    }

    state.lifePoints = defaultPlayerLifePoints();
    if (state.hasSpawnpoint) {
        state.cameraPosition = state.spawnpointPosition;
    }
    state.velocity = glm::vec3(0.0f);
}

json serializePortalTrackerState(
    const PlayerData::PortalTrackerState& state) {
    return json{
        {"trackingActive", state.trackingActive},
        {"trackedBlock", {
            {"x", state.trackedBlock.x},
            {"y", state.trackedBlock.y},
            {"z", state.trackedBlock.z},
        }},
        {"trackedWorldSeed", state.trackedWorldSeed},
        {"trackedWorldBiome", serializeBiomeSelection(state.trackedWorldBiome)},
        {"trackedChildSeed", state.trackedChildSeed},
        {"trackedChildBiome", serializeBiomeSelection(state.trackedChildBiome)},
        {"trackedUniverseName", state.trackedUniverseName},
    };
}

bool deserializePlayerState(const json& value, Player::PersistentState& outState) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("cameraPosition")) {
        deserializeVec3(value["cameraPosition"], outState.cameraPosition);
    }
    if (value.contains("cameraOrientation")) {
        deserializeQuat(value["cameraOrientation"], outState.cameraOrientation);
    }
    if (value.contains("velocity")) {
        deserializeVec3(value["velocity"], outState.velocity);
    }
    if (value.contains("lifePoints") && value["lifePoints"].is_number_integer()) {
        outState.lifePoints = value["lifePoints"].get<int>();
    }
    if (value.contains("experiencePoints") && value["experiencePoints"].is_number()) {
        outState.experiencePoints = value["experiencePoints"].get<float>();
    }
    if (value.contains("experienceLevel") && value["experienceLevel"].is_number_integer()) {
        outState.experienceLevel = value["experienceLevel"].get<int>();
    }
    if (value.contains("experiencePerBlock") && value["experiencePerBlock"].is_number()) {
        outState.experiencePerBlock = value["experiencePerBlock"].get<float>();
    }
    if (value.contains("sandboxModeEnabled") &&
        value["sandboxModeEnabled"].is_boolean()) {
        outState.sandboxModeEnabled = value["sandboxModeEnabled"].get<bool>();
    }
    if (value.contains("universeCreationCooldownRemainingSeconds") &&
        value["universeCreationCooldownRemainingSeconds"].is_number()) {
        outState.universeCreationCooldownRemainingSeconds =
            value["universeCreationCooldownRemainingSeconds"].get<double>();
    }
    if (value.contains("doubleJumpCooldownRemainingSeconds") &&
        value["doubleJumpCooldownRemainingSeconds"].is_number()) {
        outState.doubleJumpCooldownRemainingSeconds =
            value["doubleJumpCooldownRemainingSeconds"].get<double>();
    }
    if (value.contains("hasSpawnpoint") && value["hasSpawnpoint"].is_boolean()) {
        outState.hasSpawnpoint = value["hasSpawnpoint"].get<bool>();
    }
    if (value.contains("spawnpointPosition")) {
        deserializeVec3(value["spawnpointPosition"], outState.spawnpointPosition);
    }
    if (value.contains("spawnpointUniverseSeed") &&
        value["spawnpointUniverseSeed"].is_number_integer()) {
        outState.spawnpointUniverseSeed =
            value["spawnpointUniverseSeed"].get<std::uint32_t>();
    }
    if (value.contains("spawnpointBiomeSelection")) {
        deserializeBiomeSelection(value["spawnpointBiomeSelection"],
                                  outState.spawnpointBiomeSelection);
    }
    if (value.contains("spawnpointTraversalStack") &&
        value["spawnpointTraversalStack"].is_array()) {
        for (const json& entry : value["spawnpointTraversalStack"]) {
            WorldLevel level{};
            if (deserializeWorldLevel(entry, level)) {
                outState.spawnpointTraversalStack.push_back(std::move(level));
            }
        }
    }
    if (value.contains("grounded") && value["grounded"].is_boolean()) {
        outState.grounded = value["grounded"].get<bool>();
    }
    if (value.contains("crouching") && value["crouching"].is_boolean()) {
        outState.crouching = value["crouching"].get<bool>();
    }
    if (value.contains("currentEyeHeight") &&
        value["currentEyeHeight"].is_number()) {
        outState.currentEyeHeight = value["currentEyeHeight"].get<float>();
    }
    if (value.contains("headBobPhase") && value["headBobPhase"].is_number()) {
        outState.headBobPhase = value["headBobPhase"].get<float>();
    }
    if (value.contains("headBobBlend") && value["headBobBlend"].is_number()) {
        outState.headBobBlend = value["headBobBlend"].get<float>();
    }
    if (value.contains("headBobLocalOffset")) {
        deserializeVec3(value["headBobLocalOffset"], outState.headBobLocalOffset);
    }
    if (value.contains("headBobRollRadians") &&
        value["headBobRollRadians"].is_number()) {
        outState.headBobRollRadians = value["headBobRollRadians"].get<float>();
    }
    if (value.contains("lastFootstepPhase") &&
        value["lastFootstepPhase"].is_number()) {
        outState.lastFootstepPhase = value["lastFootstepPhase"].get<float>();
    }
    if (value.contains("damageRollTimer") && value["damageRollTimer"].is_number()) {
        outState.damageRollTimer = value["damageRollTimer"].get<float>();
    }
    if (value.contains("damageRollRadiansCurrent") &&
        value["damageRollRadiansCurrent"].is_number()) {
        outState.damageRollRadiansCurrent = value["damageRollRadiansCurrent"].get<float>();
    }
    if (value.contains("lifeFlashTimer") && value["lifeFlashTimer"].is_number()) {
        outState.lifeFlashTimer = value["lifeFlashTimer"].get<float>();
    }
    if (value.contains("hotbarState")) {
        deserializeHotbarState(value["hotbarState"], outState.hotbarState);
    }

    sanitizeDeathState(outState);
    return true;
}

bool deserializePortalTrackerState(
    const json& value,
    PlayerData::PortalTrackerState& outState) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("trackingActive") && value["trackingActive"].is_boolean()) {
        outState.trackingActive = value["trackingActive"].get<bool>();
    }
    if (value.contains("trackedBlock") && value["trackedBlock"].is_object()) {
        const auto& block = value["trackedBlock"];
        if (block.contains("x") && block["x"].is_number_integer()) {
            outState.trackedBlock.x = block["x"].get<int>();
        }
        if (block.contains("y") && block["y"].is_number_integer()) {
            outState.trackedBlock.y = block["y"].get<int>();
        }
        if (block.contains("z") && block["z"].is_number_integer()) {
            outState.trackedBlock.z = block["z"].get<int>();
        }
    }
    if (value.contains("trackedWorldSeed") &&
        value["trackedWorldSeed"].is_number_integer()) {
        outState.trackedWorldSeed = value["trackedWorldSeed"].get<std::uint32_t>();
    }
    if (value.contains("trackedWorldBiome")) {
        deserializeBiomeSelection(value["trackedWorldBiome"],
                                  outState.trackedWorldBiome);
    }
    if (value.contains("trackedChildSeed") &&
        value["trackedChildSeed"].is_number_integer()) {
        outState.trackedChildSeed = value["trackedChildSeed"].get<std::uint32_t>();
    }
    if (value.contains("trackedChildBiome")) {
        deserializeBiomeSelection(value["trackedChildBiome"],
                                  outState.trackedChildBiome);
    }
    if (value.contains("trackedUniverseName") &&
        value["trackedUniverseName"].is_string()) {
        outState.trackedUniverseName = value["trackedUniverseName"].get<std::string>();
    }

    return true;
}

json serializeWorldLevel(const WorldLevel& level) {
    return json{
        {"seed", level.seed},
        {"biomeSelection", serializeBiomeSelection(level.biomeSelection)},
        {"returnPosition", serializeVec3(level.returnPosition)},
        {"returnOrientation", serializeQuat(level.returnOrientation)},
        {"portalBlock", {
            {"x", level.portalBlock.x},
            {"y", level.portalBlock.y},
            {"z", level.portalBlock.z},
        }},
        {"portalNormal", {
            {"x", level.portalNormal.x},
            {"y", level.portalNormal.y},
            {"z", level.portalNormal.z},
        }},
    };
}

bool deserializeWorldLevel(const json& value, WorldLevel& outLevel) {
    if (!value.is_object()) {
        return false;
    }

    if (value.contains("seed") && value["seed"].is_number_integer()) {
        outLevel.seed = value["seed"].get<std::uint32_t>();
    }
    if (value.contains("biomeSelection")) {
        deserializeBiomeSelection(value["biomeSelection"], outLevel.biomeSelection);
    }
    outLevel.biomePreset = resolvePreset(outLevel.biomeSelection);

    if (value.contains("returnPosition")) {
        deserializeVec3(value["returnPosition"], outLevel.returnPosition);
    }
    if (value.contains("returnOrientation")) {
        deserializeQuat(value["returnOrientation"], outLevel.returnOrientation);
    }
    if (value.contains("portalBlock") && value["portalBlock"].is_object()) {
        const auto& portalBlock = value["portalBlock"];
        if (portalBlock.contains("x") && portalBlock["x"].is_number_integer()) {
            outLevel.portalBlock.x = portalBlock["x"].get<int>();
        }
        if (portalBlock.contains("y") && portalBlock["y"].is_number_integer()) {
            outLevel.portalBlock.y = portalBlock["y"].get<int>();
        }
        if (portalBlock.contains("z") && portalBlock["z"].is_number_integer()) {
            outLevel.portalBlock.z = portalBlock["z"].get<int>();
        }
    }
    if (value.contains("portalNormal") && value["portalNormal"].is_object()) {
        const auto& portalNormal = value["portalNormal"];
        if (portalNormal.contains("x") && portalNormal["x"].is_number_integer()) {
            outLevel.portalNormal.x = portalNormal["x"].get<int>();
        }
        if (portalNormal.contains("y") && portalNormal["y"].is_number_integer()) {
            outLevel.portalNormal.y = portalNormal["y"].get<int>();
        }
        if (portalNormal.contains("z") && portalNormal["z"].is_number_integer()) {
            outLevel.portalNormal.z = portalNormal["z"].get<int>();
        }
    }

    return true;
}

bool writeJsonFile(const std::filesystem::path& path, const json& value,
                   std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create directory: " + path.parent_path().string();
        }
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (outError) {
            *outError = "Failed to open file for writing: " + path.string();
        }
        return false;
    }

    output << value.dump(2);
    if (!output) {
        if (outError) {
            *outError = "Failed to write file: " + path.string();
        }
        return false;
    }

    return true;
}

bool readJsonFile(const std::filesystem::path& path, json& outValue,
                  std::string* outError) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (outError) {
            *outError = "Failed to open file: " + path.string();
        }
        return false;
    }

    try {
        outValue = json::parse(input, nullptr, true, true);
    } catch (const std::exception& exception) {
        if (outError) {
            *outError = "Failed to parse file: " + path.string() + " (" +
                        exception.what() + ")";
        }
        return false;
    }

    return true;
}

WorldLevel makeRootTraversalLevel(const WorldManifest& manifest) {
    WorldLevel level{};
    level.seed = manifest.rootSeed;
    level.biomeSelection = manifest.rootBiomeSelection;
    level.biomePreset = resolvePreset(manifest.rootBiomeSelection);
    level.returnPosition = glm::vec3(0.0f);
    level.returnOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    level.portalBlock = glm::ivec3(0);
    level.portalNormal = glm::ivec3(0);
    return level;
}

std::uint32_t generateWorldSeed() {
    static std::random_device randomDevice;
    static std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<std::uint32_t> distribution(
        1u, std::numeric_limits<std::uint32_t>::max());
    return distribution(generator);
}

std::string currentTimestampString() {
    const std::time_t nowTime = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

void logSaveEvent(const std::string& action, const WorldSession& session,
                  double totalPlaytimeSeconds) {
    std::printf("[Save] %s | %s | World: %s | Directory: %s | Playtime: %.2fs\n",
                currentTimestampString().c_str(), action.c_str(),
                session.manifest.displayName.c_str(),
                session.paths.worldDirectory.string().c_str(),
                std::max(0.0, totalPlaytimeSeconds));
    std::fflush(stdout);
}

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
        BlockId blockType = BlockIds::AIR;
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

std::uint64_t countNamedUniverseFiles(const WorldPaths& paths) {
    std::uint64_t namedUniverseCount = 0;
    std::error_code ec;

    if (!std::filesystem::exists(paths.universesDirectory, ec)) {
        return 0;
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(paths.universesDirectory, ec)) {
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

std::filesystem::path worldsRoot() {
    return AppPaths::saveWorldsRoot();
}

WorldPaths buildWorldPaths(const std::filesystem::path& worldDirectory) {
    WorldPaths paths;
    paths.worldDirectory = worldDirectory;
    paths.worldFilePath = worldDirectory / kWorldFileName;
    paths.universesDirectory = worldDirectory / kUniversesDirectoryName;
    paths.playerDataDirectory = worldDirectory / kPlayerDataDirectoryName;
    paths.playerDataFilePath = paths.playerDataDirectory / kPlayerDataFileName;
    paths.statsDirectory = worldDirectory / kStatsDirectoryName;
    paths.statsFilePath = paths.statsDirectory / kStatsFileName;
    return paths;
}

std::string sanitizeDisplayName(const std::string& displayName) {
    std::string value = trimCopy(displayName);
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char ch) { return ch < 32; }),
                value.end());
    if (value.empty()) {
        return "New World";
    }
    return value;
}

std::string sanitizeFolderName(const std::string& displayName) {
    const std::string value = sanitizeDisplayName(displayName);
    std::string folderName;
    folderName.reserve(value.size());

    bool previousSeparator = false;
    for (unsigned char ch : value) {
        if (isInvalidFolderChar(ch)) {
            if (!previousSeparator) {
                folderName.push_back('_');
                previousSeparator = true;
            }
            continue;
        }

        if (std::isspace(ch)) {
            if (!previousSeparator) {
                folderName.push_back(' ');
                previousSeparator = true;
            }
            continue;
        }

        folderName.push_back(static_cast<char>(ch));
        previousSeparator = false;
    }

    while (!folderName.empty() &&
           (folderName.front() == ' ' || folderName.front() == '.')) {
        folderName.erase(folderName.begin());
    }
    while (!folderName.empty() &&
           (folderName.back() == ' ' || folderName.back() == '.')) {
        folderName.pop_back();
    }

    if (folderName.empty()) {
        return "New World";
    }
    return folderName;
}

std::filesystem::path uniqueWorldDirectoryFor(const std::string& displayName) {
    const std::string sanitized = sanitizeFolderName(displayName);
    std::error_code ec;
    std::filesystem::create_directories(worldsRoot(), ec);

    std::filesystem::path candidate = worldsRoot() / sanitized;
    if (!std::filesystem::exists(candidate, ec)) {
        return candidate;
    }

    for (std::uint32_t suffix = 2; suffix < 10000; ++suffix) {
        candidate = worldsRoot() /
                    (sanitized + " (" + std::to_string(suffix) + ")");
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }

    return worldsRoot() / (sanitized + " (" + std::to_string(generateWorldSeed()) + ")");
}

bool saveWorldManifest(const WorldPaths& paths, const WorldManifest& manifest,
                       std::string* outError) {
    const json value{
        {"schemaVersion", manifest.schemaVersion},
        {"displayName", sanitizeDisplayName(manifest.displayName)},
        {"rootSeed", manifest.rootSeed},
        {"rootBiomeSelection", serializeBiomeSelection(manifest.rootBiomeSelection)},
        {"activeUniverseSeed", manifest.activeUniverseSeed},
        {"activeUniverseBiomeSelection",
         serializeBiomeSelection(manifest.activeUniverseBiomeSelection)},
    };
    return writeJsonFile(paths.worldFilePath, value, outError);
}

bool loadWorldManifest(const WorldPaths& paths, WorldManifest& outManifest,
                       std::string* outError) {
    json value;
    if (!readJsonFile(paths.worldFilePath, value, outError)) {
        return false;
    }
    if (!value.is_object()) {
        if (outError) {
            *outError = "Invalid world manifest: " + paths.worldFilePath.string();
        }
        return false;
    }

    WorldManifest manifest{};
    if (value.contains("schemaVersion") && value["schemaVersion"].is_number_integer()) {
        manifest.schemaVersion = value["schemaVersion"].get<std::uint32_t>();
    }
    if (value.contains("displayName") && value["displayName"].is_string()) {
        manifest.displayName = sanitizeDisplayName(value["displayName"].get<std::string>());
    }
    if (value.contains("rootSeed") && value["rootSeed"].is_number_integer()) {
        manifest.rootSeed = value["rootSeed"].get<std::uint32_t>();
    }
    if (value.contains("rootBiomeSelection")) {
        if (!deserializeBiomeSelection(value["rootBiomeSelection"],
                                       manifest.rootBiomeSelection)) {
            if (outError) {
                *outError = "Invalid root biome selection in: " +
                            paths.worldFilePath.string();
            }
            return false;
        }
    }
    if (manifest.rootSeed == 0 || manifest.rootBiomeSelection.presetId.empty()) {
        if (outError) {
            *outError = "World manifest is missing root data: " +
                        paths.worldFilePath.string();
        }
        return false;
    }

    if (value.contains("activeUniverseSeed") &&
        value["activeUniverseSeed"].is_number_integer()) {
        manifest.activeUniverseSeed = value["activeUniverseSeed"].get<std::uint32_t>();
    } else {
        manifest.activeUniverseSeed = manifest.rootSeed;
    }

    if (value.contains("activeUniverseBiomeSelection")) {
        BiomeSelection activeSelection;
        if (deserializeBiomeSelection(value["activeUniverseBiomeSelection"],
                                      activeSelection)) {
            manifest.activeUniverseBiomeSelection = activeSelection;
        } else {
            manifest.activeUniverseBiomeSelection = manifest.rootBiomeSelection;
        }
    } else {
        manifest.activeUniverseBiomeSelection = manifest.rootBiomeSelection;
    }

    outManifest = std::move(manifest);
    return true;
}

bool savePlayerData(const WorldPaths& paths, const PlayerData& playerData,
                    std::string* outError) {
    json traversalStack = json::array();
    for (const WorldLevel& level : playerData.traversalStack) {
        traversalStack.push_back(serializeWorldLevel(level));
    }

    const json value{
        {"schemaVersion", kPlayerDataVersion},
        {"playerName", sanitizeDisplayName(playerData.playerName)},
        {"hasPlayerState", playerData.hasPlayerState},
        {"playerState", serializePlayerState(playerData.playerState)},
        {"currentUniverseSeed", playerData.currentUniverseSeed},
        {"currentUniverseBiomeSelection",
         serializeBiomeSelection(playerData.currentUniverseBiomeSelection)},
        {"hasPortalTrackerState", playerData.hasPortalTrackerState},
        {"portalTrackerState",
         playerData.hasPortalTrackerState
             ? serializePortalTrackerState(playerData.portalTrackerState)
             : json::object()},
        {"traversalStack", traversalStack},
    };
    return writeJsonFile(paths.playerDataFilePath, value, outError);
}

bool loadPlayerData(const WorldPaths& paths, PlayerData& outPlayerData,
                    std::string* outError) {
    json value;
    if (!readJsonFile(paths.playerDataFilePath, value, outError)) {
        return false;
    }
    if (!value.is_object()) {
        if (outError) {
            *outError = "Invalid player data: " + paths.playerDataFilePath.string();
        }
        return false;
    }

    PlayerData data{};
    if (value.contains("playerName") && value["playerName"].is_string()) {
        data.playerName = sanitizeDisplayName(value["playerName"].get<std::string>());
    }
    if (value.contains("hasPlayerState") && value["hasPlayerState"].is_boolean()) {
        data.hasPlayerState = value["hasPlayerState"].get<bool>();
    }
    if (value.contains("playerState")) {
        deserializePlayerState(value["playerState"], data.playerState);
    }
    if (value.contains("currentUniverseSeed") &&
        value["currentUniverseSeed"].is_number_integer()) {
        data.currentUniverseSeed = value["currentUniverseSeed"].get<std::uint32_t>();
    }
    if (value.contains("currentUniverseBiomeSelection")) {
        deserializeBiomeSelection(value["currentUniverseBiomeSelection"],
                                  data.currentUniverseBiomeSelection);
    }
    if (value.contains("hasPortalTrackerState") &&
        value["hasPortalTrackerState"].is_boolean()) {
        data.hasPortalTrackerState = value["hasPortalTrackerState"].get<bool>();
    }
    if (value.contains("portalTrackerState")) {
        deserializePortalTrackerState(value["portalTrackerState"],
                                      data.portalTrackerState);
    }
    if (value.contains("traversalStack") && value["traversalStack"].is_array()) {
        for (const auto& entry : value["traversalStack"]) {
            WorldLevel level{};
            if (deserializeWorldLevel(entry, level)) {
                data.traversalStack.push_back(std::move(level));
            }
        }
    }

    outPlayerData = std::move(data);
    return true;
}

bool readGameplayStatsNumber(const json& value, const char* key,
                             double& outNumber) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_number()) {
        return false;
    }

    outNumber = std::max(0.0, value[key].get<double>());
    return true;
}

bool readGameplayStatsNumber(const json& value, const char* key,
                             float& outNumber) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_number()) {
        return false;
    }

    outNumber = value[key].get<float>();
    return true;
}

bool readGameplayStatsCount(const json& value, const char* key,
                            std::uint64_t& outCount) {
    if (!value.is_object() || !value.contains(key) ||
        !value[key].is_number_integer()) {
        return false;
    }

    const json& countValue = value[key];
    if (countValue.is_number_unsigned()) {
        outCount = countValue.get<std::uint64_t>();
        return true;
    }

    const long long signedCount = countValue.get<long long>();
    if (signedCount < 0) {
        return false;
    }

    outCount = static_cast<std::uint64_t>(signedCount);
    return true;
}

void readGameplayAcquisitionStatsObject(
    const json& value,
    std::map<std::string, std::uint64_t>& outCounts
) {
    if (!value.is_object()) {
        return;
    }

    for (auto it = value.begin(); it != value.end(); ++it) {
        std::uint64_t count = 0;
        if (!readGameplayStatsCount(value, it.key().c_str(), count)) {
            continue;
        }

        outCounts[it.key()] = count;
    }
}

void readGameplayAcquisitionStatsCategory(
    const json& category,
    GameplayStatus::PersistentState& outStats
) {
    if (!category.is_object()) {
        return;
    }

    if (category.contains("blocks")) {
        readGameplayAcquisitionStatsObject(
            category["blocks"],
            outStats.acquiredBlockCounts
        );
    }

    if (category.contains("items")) {
        readGameplayAcquisitionStatsObject(
            category["items"],
            outStats.acquiredItemCounts
        );
    }
}

json serializeBlockAcquisitionStats(
    const std::map<std::string, std::uint64_t>& acquiredBlockCounts
) {
    json value = json::object();

    for (const BlockCatalogEntry& entry : BlockCatalog::entries()) {
        value[entry.stableId] = 0;
    }

    for (const auto& [blockId, count] : acquiredBlockCounts) {
        if (!blockId.empty()) {
            value[blockId] = count;
        }
    }

    return value;
}

json serializeItemAcquisitionStats(
    const std::map<std::string, std::uint64_t>& acquiredItemCounts
) {
    json value = json::object();

    for (const ItemCatalogEntry& entry : ItemCatalog::entries()) {
        value[entry.stableId] = 0;
    }

    for (const auto& [itemId, count] : acquiredItemCounts) {
        if (!itemId.empty()) {
            value[itemId] = count;
        }
    }

    return value;
}

void readGameplayStatsFlatSchema(const json& value,
                                 GameplayStatus::PersistentState& outStats) {
    readGameplayStatsNumber(value, "playtimeSeconds", outStats.playtimeSeconds);
    readGameplayStatsNumber(value, "playerXp", outStats.playerXp);
    readGameplayStatsNumber(value, "totalPlayerXpEarned", outStats.totalPlayerXpEarned);
    if (value.contains("playerLevel") && value["playerLevel"].is_number_integer()) {
        outStats.playerLevel = value["playerLevel"].get<int>();
    }
    readGameplayStatsCount(value, "deathCount", outStats.deathCount);
    readGameplayStatsCount(value, "blocksBrokenCount", outStats.blocksBrokenCount);
    readGameplayStatsCount(value, "blocksPlacedCount", outStats.blocksPlacedCount);
    readGameplayStatsCount(value, "universesCreatedCount",
                           outStats.universesCreatedCount);
    readGameplayStatsCount(value, "currentUniversesCount",
                           outStats.currentUniversesCount);
    readGameplayStatsCount(value, "guySpawnCount", outStats.guySpawnCount);
    readGameplayStatsCount(value, "guyAttackCount", outStats.guyAttackCount);

    if (value.contains("acquiredBlockCounts")) {
        readGameplayAcquisitionStatsObject(
            value["acquiredBlockCounts"],
            outStats.acquiredBlockCounts
        );
    }

    if (value.contains("acquiredItemCounts")) {
        readGameplayAcquisitionStatsObject(
            value["acquiredItemCounts"],
            outStats.acquiredItemCounts
        );
    }
}

void readGameplayStatsCategory(const json& category,
                               GameplayStatus::PersistentState& outStats) {
    if (!category.is_object()) {
        return;
    }

    readGameplayStatsNumber(category, "playtimeSeconds", outStats.playtimeSeconds);
    readGameplayStatsNumber(category, "playerXp", outStats.playerXp);
    readGameplayStatsNumber(category, "totalPlayerXpEarned", outStats.totalPlayerXpEarned);
    if (category.contains("playerLevel") && category["playerLevel"].is_number_integer()) {
        outStats.playerLevel = category["playerLevel"].get<int>();
    }
    readGameplayStatsCount(category, "deathCount", outStats.deathCount);
    readGameplayStatsCount(category, "blocksBrokenCount", outStats.blocksBrokenCount);
    readGameplayStatsCount(category, "blocksPlacedCount", outStats.blocksPlacedCount);
    readGameplayStatsCount(category, "universesCreatedCount",
                           outStats.universesCreatedCount);
    readGameplayStatsCount(category, "currentUniversesCount",
                           outStats.currentUniversesCount);
    readGameplayStatsCount(category, "guySpawnCount", outStats.guySpawnCount);
    readGameplayStatsCount(category, "guyAttackCount", outStats.guyAttackCount);
}

void readGameplayStatsNestedSchema(const json& value,
                                   GameplayStatus::PersistentState& outStats) {
    if (value.contains("player")) {
        readGameplayStatsCategory(value["player"], outStats);
    }
    if (value.contains("blocks")) {
        readGameplayStatsCategory(value["blocks"], outStats);
    }
    if (value.contains("world")) {
        readGameplayStatsCategory(value["world"], outStats);
    }
    if (value.contains("enemies")) {
        readGameplayStatsCategory(value["enemies"], outStats);
    }
    if (value.contains("acquired")) {
        readGameplayAcquisitionStatsCategory(value["acquired"], outStats);
    }
}

json serializeGameplayStats(const GameplayStatus::PersistentState& gameplayStats) {
    const GameplayStatus::PersistentState sanitizedStats =
        GameplayStatus::sanitizePersistentState(gameplayStats);

    return json{
        {"version", GameplayStatus::kStatsVersion},
        {"player", {
             {"playtimeSeconds", sanitizedStats.playtimeSeconds},
             {"playerXp", sanitizedStats.playerXp},
             {"totalPlayerXpEarned", sanitizedStats.totalPlayerXpEarned},
             {"playerLevel", sanitizedStats.playerLevel},
             {"deathCount", sanitizedStats.deathCount},
         }},
        {"blocks", {
             {"blocksBrokenCount", sanitizedStats.blocksBrokenCount},
             {"blocksPlacedCount", sanitizedStats.blocksPlacedCount},
         }},
        {"acquired", {
             {"blocks", serializeBlockAcquisitionStats(sanitizedStats.acquiredBlockCounts)},
             {"items", serializeItemAcquisitionStats(sanitizedStats.acquiredItemCounts)},
         }},
        {"world", {
             {"universesCreatedCount", sanitizedStats.universesCreatedCount},
             {"currentUniversesCount", sanitizedStats.currentUniversesCount},
         }},
        {"enemies", {
             {"guySpawnCount", sanitizedStats.guySpawnCount},
             {"guyAttackCount", sanitizedStats.guyAttackCount},
         }},
    };
}

bool saveStats(const WorldPaths& paths,
               const GameplayStatus::PersistentState& gameplayStats,
               std::string* outError) {
    const json value = serializeGameplayStats(gameplayStats);
    return writeJsonFile(paths.statsFilePath, value, outError);
}

bool loadStats(const WorldPaths& paths, std::uint32_t rootUniverseSeed,
               GameplayStatus::PersistentState& outGameplayStats,
               std::string* outError) {
    (void)rootUniverseSeed;
    json value;
    if (!std::filesystem::exists(paths.statsFilePath)) {
        outGameplayStats = GameplayStatus::sanitizePersistentState(
            GameplayStatus::PersistentState{});
        return true;
    }

    if (!readJsonFile(paths.statsFilePath, value, outError)) {
        outGameplayStats = GameplayStatus::sanitizePersistentState(
            GameplayStatus::PersistentState{});
        return false;
    }

    if (!value.is_object()) {
        outGameplayStats = GameplayStatus::sanitizePersistentState(
            GameplayStatus::PersistentState{});
        return false;
    }

    GameplayStatus::PersistentState loadedStats{};

    readGameplayStatsFlatSchema(value, loadedStats);
    readGameplayStatsNestedSchema(value, loadedStats);
    loadedStats.currentUniversesCount = countNamedUniverseFiles(paths);

    outGameplayStats = GameplayStatus::sanitizePersistentState(loadedStats);
    return true;
}

bool createWorld(const std::string& displayName,
                 const BiomeSelection& rootBiomeSelection,
                 WorldSession& outSession,
                 std::string* outError) {
    WorldSession session{};
    session.paths = buildWorldPaths(uniqueWorldDirectoryFor(displayName));
    session.manifest.schemaVersion = kWorldManifestVersion;
    session.manifest.displayName = sanitizeDisplayName(displayName);
    session.manifest.rootSeed = generateWorldSeed();
    session.manifest.rootBiomeSelection = rootBiomeSelection;
    session.manifest.activeUniverseSeed = session.manifest.rootSeed;
    session.manifest.activeUniverseBiomeSelection = rootBiomeSelection;
    session.rootPreset = resolvePreset(rootBiomeSelection);
    session.gameplayStats = GameplayStatus::sanitizePersistentState(
        GameplayStatus::PersistentState{});
    session.startUniverseSeed = session.manifest.rootSeed;
    session.startUniverseBiomeSelection = rootBiomeSelection;
    session.hasPlayerData = false;
    session.playerData.playerName = kDefaultPlayerName;
    session.playerData.hasPlayerState = false;
    session.playerData.currentUniverseSeed = session.manifest.rootSeed;
    session.playerData.currentUniverseBiomeSelection = rootBiomeSelection;
    session.playerData.traversalStack.clear();
    session.playerData.traversalStack.push_back(
        makeRootTraversalLevel(session.manifest));

    std::error_code ec;
    std::filesystem::create_directories(session.paths.worldDirectory, ec);
    std::filesystem::create_directories(session.paths.universesDirectory, ec);
    std::filesystem::create_directories(session.paths.playerDataDirectory, ec);
    std::filesystem::create_directories(session.paths.statsDirectory, ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create world directory: " +
                        session.paths.worldDirectory.string();
        }
        return false;
    }

    if (!saveWorldManifest(session.paths, session.manifest, outError)) {
        return false;
    }
    if (!savePlayerData(session.paths, session.playerData, outError)) {
        return false;
    }
    if (!saveStats(session.paths, session.gameplayStats, outError)) {
        return false;
    }

    logSaveEvent("World created and saved", session,
                 session.gameplayStats.playtimeSeconds);
    outSession = std::move(session);
    return true;
}

bool deleteWorld(const std::filesystem::path& worldDirectory,
                 std::string* outError) {
    std::filesystem::path root;
    std::filesystem::path target;
    if (!resolveWorldDirectoryPath(worldDirectory, root, target, outError)) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        if (outError) {
            *outError = "World directory does not exist: " + target.string();
        }
        return false;
    }

    const std::uintmax_t removedCount = std::filesystem::remove_all(target, ec);
    if (ec || removedCount == 0) {
        if (outError) {
            *outError = "Failed to delete world directory: " + target.string();
        }
        return false;
    }

    return true;
}

bool renameWorld(const std::filesystem::path& worldDirectory,
                 const std::string& displayName,
                 std::filesystem::path* outRenamedWorldDirectory,
                 std::string* outError) {
    std::filesystem::path root;
    std::filesystem::path sourceDirectory;
    if (!resolveWorldDirectoryPath(worldDirectory, root, sourceDirectory, outError)) {
        return false;
    }

    WorldManifest manifest{};
    const WorldPaths sourcePaths = buildWorldPaths(sourceDirectory);
    if (!loadWorldManifest(sourcePaths, manifest, outError)) {
        return false;
    }

    manifest.displayName = sanitizeDisplayName(displayName);

    const std::filesystem::path targetDirectory =
        uniqueWorldDirectoryForRename(sourceDirectory, manifest.displayName);
    std::error_code ec;

    if (sourceDirectory != targetDirectory) {
        std::filesystem::rename(sourceDirectory, targetDirectory, ec);
        if (ec) {
            if (outError) {
                *outError = "Failed to rename world directory: " + sourceDirectory.string();
            }
            return false;
        }
    }

    const WorldPaths targetPaths = buildWorldPaths(targetDirectory);
    if (!saveWorldManifest(targetPaths, manifest, outError)) {
        return false;
    }

    if (outRenamedWorldDirectory) {
        *outRenamedWorldDirectory = targetDirectory;
    }
    return true;
}

bool loadPlayerAndWorldSession(const std::filesystem::path& worldDirectory,
                               WorldSession& outSession,
                               std::string* outError) {
    WorldSession session{};
    session.paths = buildWorldPaths(worldDirectory);

    if (!loadWorldManifest(session.paths, session.manifest, outError)) {
        return false;
    }

    session.rootPreset = resolvePreset(session.manifest.rootBiomeSelection);
    session.startUniverseSeed = session.manifest.activeUniverseSeed != 0
                                    ? session.manifest.activeUniverseSeed
                                    : session.manifest.rootSeed;
    session.startUniverseBiomeSelection =
        session.manifest.activeUniverseBiomeSelection.presetId.empty()
            ? session.manifest.rootBiomeSelection
            : session.manifest.activeUniverseBiomeSelection;

    loadStats(session.paths, session.manifest.rootSeed, session.gameplayStats,
              nullptr);

    if (!loadPlayerData(session.paths, session.playerData, nullptr)) {
        session.playerData = {};
        session.playerData.playerName = kDefaultPlayerName;
        session.playerData.hasPlayerState = false;
    }

    if (session.playerData.traversalStack.empty()) {
        session.playerData.traversalStack.push_back(
            makeRootTraversalLevel(session.manifest));
    }

    if (session.playerData.currentUniverseSeed == 0) {
        session.playerData.currentUniverseSeed = session.startUniverseSeed;
    }
    if (session.playerData.currentUniverseBiomeSelection.presetId.empty()) {
        session.playerData.currentUniverseBiomeSelection =
            session.startUniverseBiomeSelection;
    }

    session.hasPlayerData = session.playerData.hasPlayerState;
    outSession = std::move(session);
    return true;
}

bool loadWorld(const std::filesystem::path& worldDirectory,
               WorldSession& outSession,
               std::string* outError) {
    return loadPlayerAndWorldSession(worldDirectory, outSession, outError);
}

bool saveSessionWithPlayerState(const WorldSession& session,
                                const Player::PersistentState& playerState,
                                const glm::vec3& cameraPosition,
                                const glm::quat& cameraOrientation,
                                WorldStack& worldStack,
                                const GameplayStatus::PersistentState& gameplayStats,
                                std::string* outError) {
    if (!worldStack.currentWorld()) {
        if (outError) {
            *outError = "No active world is available for saving.";
        }
        return false;
    }

    WorldManifest manifest = session.manifest;
    manifest.activeUniverseSeed = worldStack.currentWorld()->seed;
    manifest.activeUniverseBiomeSelection = worldStack.currentWorld()->biomeSelection;

    PlayerData playerData = session.playerData;
    playerData.hasPlayerState = true;
    playerData.playerState = playerState;
    playerData.playerState.cameraPosition = cameraPosition;
    playerData.playerState.cameraOrientation = cameraOrientation;
    playerData.currentUniverseSeed = worldStack.currentWorld()->seed;
    playerData.currentUniverseBiomeSelection = worldStack.currentWorld()->biomeSelection;
    playerData.traversalStack = worldStack.snapshotTraversalStack();
    if (playerData.traversalStack.empty()) {
        playerData.traversalStack.push_back(makeRootTraversalLevel(manifest));
    }

    worldStack.saveActivePlayerState(cameraPosition, cameraOrientation);
    worldStack.saveCurrentWorldEdits();

    GameplayStatus::PersistentState gameplayStatsToSave =
        GameplayStatus::sanitizePersistentState(gameplayStats);
    gameplayStatsToSave.currentUniversesCount = worldStack.countNamedUniverses();

    if (!saveWorldManifest(session.paths, manifest, outError)) {
        return false;
    }
    if (!savePlayerData(session.paths, playerData, outError)) {
        return false;
    }
    if (!saveStats(session.paths, gameplayStatsToSave, outError)) {
        return false;
    }

    logSaveEvent("World session saved", session, gameplayStatsToSave.playtimeSeconds);
    return true;
}

bool saveSession(const WorldSession& session, const Player& player,
                 WorldStack& worldStack,
                 const GameplayStatus::PersistentState& gameplayStats,
                 std::string* outError) {
    return saveSessionWithPlayerState(session,
                                      player.capturePersistentState(),
                                      player.camera.position,
                                      player.camera.orientation,
                                      worldStack,
                                      gameplayStats,
                                      outError);
}

std::vector<WorldSummary> listWorlds() {
    std::vector<WorldSummary> worlds;
    const std::filesystem::path root = worldsRoot();
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) ||
        !std::filesystem::is_directory(root, ec)) {
        return worlds;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory()) {
            ec.clear();
            continue;
        }

        WorldSummary summary{};
        summary.paths = buildWorldPaths(entry.path());
        if (!loadWorldManifest(summary.paths, summary.manifest, nullptr)) {
            continue;
        }

        loadStats(summary.paths, summary.manifest.rootSeed, summary.gameplayStats,
                  nullptr);
        summary.lastWriteTime = std::filesystem::last_write_time(
            summary.paths.worldFilePath, ec);
        if (ec) {
            ec.clear();
            summary.lastWriteTime = std::filesystem::file_time_type{};
        }
        worlds.push_back(std::move(summary));
    }

std::sort(worlds.begin(), worlds.end(),
              [](const WorldSummary& a, const WorldSummary& b) {
                  if (a.lastWriteTime != b.lastWriteTime) {
                      return a.lastWriteTime > b.lastWriteTime;
                  }
                  return a.manifest.displayName < b.manifest.displayName;
              });
    return worlds;
}

}  // namespace WorldSaveService
