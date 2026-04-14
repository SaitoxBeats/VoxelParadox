// File: VoxelParadox.Client/src/World/world/block_catalog.cpp
// Purpose: loads the stable block id catalog from data with a compiled fallback.
// Flow: resolves named block ids before the block registry builds runtime definitions.

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

// 2. Third-party Libraries
#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "world/block/block_catalog.hpp"

#include "client_assets.hpp"
#include "path/app_paths.hpp"

namespace {

using json = nlohmann::json;

std::vector<BlockCatalogEntry> makeFallbackEntries() {
    return {
        { 0, "air", "air" },
        { 1, "stone", "stone" },
        { 2, "crystal", "crystal" },
        { 3, "void_matter", "void_matter" },
        { 4, "membrane", "membrane" },
        { 5, "organic", "organic" },
        { 6, "metal", "metal" },
        { 7, "portal", "portal" },
        { 8, "membrane_weave", "membrane_weave" },
        { 9, "membrane_wire", "membrane_wire" },
        { 10, "atlas_demo", "atlas_demo" },
        { 11, "cloud_chunk", "cloud_chunk" }
    };
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string normalizeId(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       if (ch == ' ' || ch == '-') {
                           return static_cast<char>('_');
                       }
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool validateEntries(const std::vector<BlockCatalogEntry>& entries) {
    if (entries.empty()) {
        return false;
    }

    std::vector<BlockCatalogEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const BlockCatalogEntry& left, const BlockCatalogEntry& right) {
                  return left.value < right.value;
              });

    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (sorted[index].value != static_cast<BlockId>(index)) {
            return false;
        }

        if (sorted[index].stableId.empty()) {
            return false;
        }
    }

    return true;
}

std::vector<BlockCatalogEntry> loadEntries() {
    std::vector<BlockCatalogEntry> fallback = makeFallbackEntries();
    const std::filesystem::path registryPath =
        AppPaths::resolve(ClientAssets::kBlockRegistryFile);
    const std::string source = readTextFile(registryPath);
    if (source.empty()) {
        return fallback;
    }

    const json root = json::parse(source, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        std::printf("[Blocks] Failed to parse %s. Using fallback block id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    const json entriesValue = root.value("entries", json{});
    if (!entriesValue.is_array()) {
        std::printf("[Blocks] Invalid block registry schema in %s. Using fallback block id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    std::vector<BlockCatalogEntry> loadedEntries;
    loadedEntries.reserve(entriesValue.size());

    for (const json& entryValue : entriesValue) {
        if (!entryValue.is_object()) {
            continue;
        }

        if (!entryValue.contains("value") || !entryValue["value"].is_number_unsigned() ||
            !entryValue.contains("id") || !entryValue["id"].is_string()) {
            continue;
        }

        BlockCatalogEntry entry{};
        entry.value = entryValue["value"].get<BlockId>();
        entry.stableId = normalizeId(entryValue["id"].get<std::string>());
        entry.folderName = entryValue.value("folder", entry.stableId);
        if (entry.folderName.empty()) {
            entry.folderName = entry.stableId;
        }

        loadedEntries.push_back(std::move(entry));
    }

    if (!validateEntries(loadedEntries)) {
        std::printf("[Blocks] Non-contiguous or invalid block ids in %s. Using fallback block id catalog.\n",
                    registryPath.string().c_str());
        return fallback;
    }

    return loadedEntries;
}

} // namespace

namespace BlockCatalog {

const std::vector<BlockCatalogEntry>& entries() {
    static const std::vector<BlockCatalogEntry> kEntries = loadEntries();
    return kEntries;
}

const BlockCatalogEntry* findByValue(BlockId blockId) {
    const auto& catalog = entries();
    const std::size_t index = static_cast<std::size_t>(blockId);
    if (index >= catalog.size()) {
        return nullptr;
    }

    return &catalog[index];
}

const BlockCatalogEntry* findByStableId(std::string_view stableId) {
    const std::string normalized = normalizeId(std::string(stableId));
    const auto& catalog = entries();
    for (const BlockCatalogEntry& entry : catalog) {
        if (entry.stableId == normalized) {
            return &entry;
        }
    }

    return nullptr;
}

bool tryResolve(std::string_view stableId, BlockId& outBlockId) {
    const BlockCatalogEntry* entry = findByStableId(stableId);
    if (!entry) {
        outBlockId = entries().front().value;
        return false;
    }

    outBlockId = entry->value;
    return true;
}

BlockId require(std::string_view stableId) {
    BlockId blockId = entries().front().value;
    tryResolve(stableId, blockId);
    return blockId;
}

std::size_t count() {
    return entries().size();
}

} // namespace BlockCatalog

NamedBlockIdRef::operator BlockId() const {
    return BlockCatalog::require(stableId);
}

BlockCountRef::operator BlockId() const {
    return static_cast<BlockId>(BlockCatalog::count());
}
