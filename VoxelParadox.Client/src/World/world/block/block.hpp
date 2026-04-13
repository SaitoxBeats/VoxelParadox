// File: VoxelParadox.Client/src/World/world/block/block.hpp
// Purpose: exposes the stable block helper API used across gameplay, rendering, and generation.
// Flow: delegates lookups to the data-driven block registry while keeping the old call surface intact.

#pragma once

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "world/block/block_catalog.hpp"
#include "world/block/block_registry.hpp"

inline const BlockDefinition& getBlockDefinition(BlockId blockId) {
    return BlockRegistry::instance().definition(blockId);
}

inline BlockProperties getBlockProperties(BlockId blockId) {
    return getBlockDefinition(blockId).properties;
}

inline bool isSolid(BlockId blockId) {
    return getBlockProperties(blockId).solid;
}

inline bool isEmissive(BlockId blockId) {
    return getBlockProperties(blockId).emitsLight;
}

inline glm::vec3 getBlockLightColor(BlockId blockId) {
    return getBlockProperties(blockId).lightColor;
}

inline float getBlockLightRadius(BlockId blockId) {
    return getBlockProperties(blockId).lightRadius;
}

inline bool usesCustomBlockModel(BlockId blockId) {
    return !getBlockDefinition(blockId).customModelAssetPath.empty();
}

inline bool isReplaceableBlock(BlockId blockId) {
    return getBlockDefinition(blockId).replaceable;
}

inline bool isPlaceableBlockId(BlockId blockId) {
    return getBlockDefinition(blockId).placeable;
}

inline bool isPlaceableBlockType(BlockId blockId) {
    return isPlaceableBlockId(blockId);
}

inline bool canTargetBlock(BlockId blockId) {
    return getBlockDefinition(blockId).targetable;
}

inline bool getBlockSelectionBounds(BlockId blockId, glm::vec3& outMin,
                                    glm::vec3& outMax) {
    if (!canTargetBlock(blockId)) {
        return false;
    }

    const BlockDefinition& definition = getBlockDefinition(blockId);
    outMin = definition.selectionBounds.min;
    outMax = definition.selectionBounds.max;
    return true;
}

inline bool canSupportTopPlacedBlock(BlockId supportBlockId, BlockId placedBlockId) {
    const BlockDefinition& placedDefinition = getBlockDefinition(placedBlockId);
    if (placedDefinition.supportRule.mode == BlockSupportMode::ALLOW_LIST) {
        return std::find(placedDefinition.supportRule.allowedSupportTypes.begin(),
                         placedDefinition.supportRule.allowedSupportTypes.end(),
                         supportBlockId) !=
               placedDefinition.supportRule.allowedSupportTypes.end();
    }

    return isSolid(supportBlockId);
}

inline std::uint32_t getBlockCategories(BlockId blockId) {
    return getBlockDefinition(blockId).categoryMask;
}

inline bool hasBlockCategory(BlockId blockId, BlockCategory category) {
    return (getBlockCategories(blockId) & static_cast<std::uint32_t>(category)) != 0u;
}

inline std::uint32_t getBlockTags(BlockId blockId) {
    return getBlockProperties(blockId).tags;
}

inline bool hasBlockTag(BlockId blockId, std::uint32_t tags) {
    return (getBlockTags(blockId) & tags) != 0u;
}

inline float getBlockHardness(BlockId blockId) {
    return getBlockProperties(blockId).hardness;
}

inline BlockData getBlockData(BlockId blockId) {
    return getBlockDefinition(blockId).data;
}

inline float getBlockBreakTimeSeconds(BlockId blockId) {
    return getBlockHardness(blockId);
}

inline const std::string& getBlockDropItemId(BlockId blockId) {
    return getBlockDefinition(blockId).data.dropItemId;
}

inline bool hasConfiguredBlockDropItem(BlockId blockId) {
    const std::string& dropItemId = getBlockDropItemId(blockId);
    return !dropItemId.empty() && dropItemId != "none";
}

inline bool canBlockDropWithoutTool(BlockId blockId) {
    return getBlockData(blockId).dropWithoutTool;
}

inline bool canBlockGrantBreakExperience(BlockId blockId) {
    return hasConfiguredBlockDropItem(blockId);
}

inline float getBlockBreakExperienceMultiplier(BlockId blockId) {
    return getBlockData(blockId).breakExperienceMultiplier;
}

inline const char* getBlockId(BlockId blockId) {
    return getBlockDefinition(blockId).id.c_str();
}

inline std::string normalizeBlockRegistryId(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       if (ch == ' ' || ch == '-') {
                           return static_cast<char>('_');
                       }
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

inline std::string normalizeBlockId(std::string value) {
    return normalizeBlockRegistryId(value);
}

inline bool tryParseBlockId(const std::string& rawValue, BlockId& outBlockId) {
    return BlockRegistry::instance().tryParseId(rawValue, outBlockId);
}

inline bool tryParseBlockType(const std::string& rawValue, BlockId& outBlockId) {
    return tryParseBlockId(rawValue, outBlockId);
}

inline const char* getBlockDisplayName(BlockId blockId) {
    return getBlockDefinition(blockId).displayName.c_str();
}

inline const char* getBlockCategoryDisplayName(BlockCategory category) {
    switch (category) {
    case BlockCategory::TERRAIN:
        return "Terrain";
    case BlockCategory::DECORATION:
        return "Decoration";
    case BlockCategory::PORTAL:
        return "Portal";
    case BlockCategory::PLANT:
        return "Plant";
    case BlockCategory::ORGANIC:
        return "Organic";
    case BlockCategory::NONE:
    default:
        return "None";
    }
}

inline std::string getBlockCategoryDisplayName(std::uint32_t categoryMask) {
    std::string result;

    const auto appendCategory = [&result, categoryMask](BlockCategory category) {
        const std::uint32_t mask = static_cast<std::uint32_t>(category);
        if ((categoryMask & mask) == 0u) {
            return;
        }

        if (!result.empty()) {
            result += " / ";
        }

        result += getBlockCategoryDisplayName(category);
    };

    appendCategory(BlockCategory::TERRAIN);
    appendCategory(BlockCategory::DECORATION);
    appendCategory(BlockCategory::PORTAL);
    appendCategory(BlockCategory::PLANT);
    appendCategory(BlockCategory::ORGANIC);

    if (result.empty()) {
        result = getBlockCategoryDisplayName(BlockCategory::NONE);
    }

    return result;
}

inline std::string getBlockCategoryDisplayName(BlockId blockId) {
    return getBlockCategoryDisplayName(getBlockCategories(blockId));
}

inline glm::vec3 hsvToRgb(float h, float s, float v) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) {
        h += 1.0f;
    }

    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;

    glm::vec3 rgb{};
    const int hi = static_cast<int>(h * 6.0f) % 6;
    switch (hi) {
    case 0:
        rgb = { c, x, 0.0f };
        break;
    case 1:
        rgb = { x, c, 0.0f };
        break;
    case 2:
        rgb = { 0.0f, c, x };
        break;
    case 3:
        rgb = { 0.0f, x, c };
        break;
    case 4:
        rgb = { x, 0.0f, c };
        break;
    default:
        rgb = { c, 0.0f, x };
        break;
    }

    return rgb + glm::vec3(m);
}

inline glm::vec3 rgbToHsv(glm::vec3 color) {
    const float maxComponent = glm::max(color.r, glm::max(color.g, color.b));
    const float minComponent = glm::min(color.r, glm::min(color.g, color.b));
    const float delta = maxComponent - minComponent;

    float hue = 0.0f;
    const float saturation = maxComponent > 0.0f ? delta / maxComponent : 0.0f;
    const float value = maxComponent;

    if (delta > 0.001f) {
        if (maxComponent == color.r) {
            hue = std::fmod((color.g - color.b) / delta, 6.0f);
        } else if (maxComponent == color.g) {
            hue = (color.b - color.r) / delta + 2.0f;
        } else {
            hue = (color.r - color.g) / delta + 4.0f;
        }

        hue /= 6.0f;
        if (hue < 0.0f) {
            hue += 1.0f;
        }
    }

    return { hue, saturation, value };
}

inline glm::vec3 getBaseColor(BlockId blockId) {
    return getBlockDefinition(blockId).baseColor;
}

inline float getBlockMaterialId(BlockId blockId) {
    return static_cast<float>(getBlockDefinition(blockId).materialId);
}

inline glm::vec4 getBlockColor(BlockId blockId, int depth, int face) {
    const glm::vec3 base = glm::max(getBaseColor(blockId), glm::vec3(0.08f));
    glm::vec3 hsv = rgbToHsv(base);

    hsv.x = std::fmod(hsv.x + depth * 0.17f, 1.0f);
    hsv.y = glm::min(1.0f, hsv.y + depth * 0.05f);
    hsv.z = glm::clamp(hsv.z + std::sin(depth * 1.3f) * 0.1f, 0.1f, 1.0f);

    const glm::vec3 shifted = hsvToRgb(hsv.x, hsv.y, hsv.z);
    glm::vec3 tint = shifted / base;

    static const float kFaceTint[] = { 0.98f, 1.02f, 0.94f, 1.05f, 0.97f, 1.01f };
    tint *= kFaceTint[face];
    tint = glm::clamp(glm::mix(glm::vec3(1.0f), tint, 0.45f), glm::vec3(0.55f),
                      glm::vec3(1.55f));

    const float emissive =
        isEmissive(blockId)
            ? glm::clamp(0.48f + std::sin(depth * 1.15f + face * 0.7f) * 0.08f,
                         0.32f, 0.62f)
            : 0.0f;
    return glm::vec4(tint, emissive);
}
