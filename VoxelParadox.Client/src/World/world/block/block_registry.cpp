// File: VoxelParadox.Client/src/World/world/block_registry.cpp
// Purpose: implements the data-driven block registry and block shader assembly.
// Flow: loads block definitions from Assets/Blocks, applies compiled fallbacks, and builds one shared shader source.

// 1. Standard Library
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <unordered_map>

// 2. Third-party Libraries
#ifndef STB_IMAGE_STATIC
#define STB_IMAGE_STATIC
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifndef STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_STATIC
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nlohmann/json.hpp>

// 3. Local Project Modules
#include "world/block/block_registry.hpp"

#include "client_assets.hpp"
#include "path/app_paths.hpp"

namespace {

using json = nlohmann::json;

constexpr const char* kFallbackStoneShader = R"(
float strata = fbm21(vec2(uv.x * 3.4 + cellHash * 4.0, worldPos.y * 0.28 + uv.y * 1.4));
float grain = noise21(localUv * 11.0 + cellHash * 9.0);
float cracks = smoothstep(0.63, 0.82,
                          fbm21(uv * 8.0 + vec2(cellHash * 17.0, -cellHash * 13.0)));
vec3 albedo = base * mix(0.72, 1.15, strata);
albedo *= mix(0.96, 0.82, cracks * 0.55);
albedo += vec3((grain - 0.5) * 0.06);
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.92, 0.05, 0.0);
)";

constexpr const char* kFallbackCrystalShader = R"(
float bands = 0.5 + 0.5 * sin((uv.x + uv.y) * 18.0 + uTime * 2.4);
float sparkle = noise21(localUv * 14.0 + cellHash * 9.0);
float fresnel = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 4.0);
vec3 albedo = mix(base * 0.78, vec3(1.0), bands * 0.32 + fresnel * 0.22);
albedo *= 0.96 + sparkle * 0.08;
albedo += vec3(0.08, 0.14, 0.18) * fresnel;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.14, 0.65,
                  0.42 + bands * 0.28 + fresnel * 0.30 + sparkle * 0.04);
)";

constexpr const char* kFallbackVoidMatterShader = R"(
float voidNoise = fbm21(uv * 6.0 + vec2(uTime * 0.20, -uTime * 0.20) + cellHash * 5.0);
float wisps = 0.5 + 0.5 * sin((uv.x - uv.y) * 14.0 - uTime * 3.0 + cellHash * 8.0);
float rim = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 2.4);
vec3 albedo = mix(base * 0.22, base * 0.85 + vec3(0.08, 0.0, 0.12), voidNoise);
albedo += vec3(0.06, 0.01, 0.08) * wisps * 0.25;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.2)), 0.52, 0.18,
                  0.12 + rim * 0.18 + wisps * 0.08);
)";

constexpr const char* kFallbackMembraneShader = R"(
float veins = fbm21(vec2(uv.x * 5.0, uv.y * 1.0 - uTime * 0.45));
float ridge = abs(sin(uv.x * 18.0 + veins * 6.0 + uTime * 2.1));
float pulseField = fbm21(uv * 0.35 + vec2(3.7, 8.1));
float pulse = 0.5 + 0.5 * sin(uTime * 5.8 + pulseField * 6.2831853);
float pores = noise21(localUv * 18.0 + cellHash * 13.0);
vec3 albedo = mix(base * 0.72, base * 1.18, smoothstep(0.36, 0.92, ridge));
albedo *= 0.95 + pores * 0.08;
albedo += vec3(0.06, 0.10, 0.05) * pulse * 0.18;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.66, 0.12,
                  0.10 + ridge * 0.10 * pulse);
)";

constexpr const char* kFallbackOrganicShader = R"(
float fiber = fbm21(vec2(uv.x * 9.0 + cellHash * 4.0, uv.y * 3.0 - cellHash * 3.0));
float pores = noise21(localUv * 18.0 + cellHash * 13.0);
vec3 albedo = mix(base * 0.78, base * 1.06, fiber);
albedo *= 0.92 + pores * 0.10;
albedo += vec3(0.05, 0.02, 0.01) * smoothstep(0.72, 1.0, pores);
return makeSample(clamp(albedo, vec3(0.0), vec3(1.25)), 0.78, 0.08, 0.0);
)";

constexpr const char* kFallbackMetalShader = R"(
float brushed = 0.5 + 0.5 * sin(uv.y * 96.0);
float scratches = fbm21(vec2(uv.x * 18.0, uv.y * 72.0));
float microScratch = noise21(localUv * 18.0 + cellHash * 7.0);
float edge = pow(1.0 - max(dot(worldNormal, viewDir), 0.0), 3.5);
vec3 albedo = mix(base * 0.72, base * 1.12,
                  brushed * 0.22 + scratches * 0.14 + microScratch * 0.04);
albedo += vec3(edge) * 0.08;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.6)), 0.18, 0.85, 0.0);
)";

constexpr const char* kFallbackPortalShader = R"(
float dist = length(centeredUv);
float ring = 0.5 + 0.5 * sin(dist * 26.0 - uTime * 2.0);
float vortex = fbm21(uv * 2.25 + vec2(uTime * 0.15, -uTime * 0.12) + vec2(cellHash * 9.0));
vec3 albedo = mix(vec3(0.06, 0.02, 0.10),
                  base * 1.20 + vec3(0.10, 0.0, 0.16), vortex);
albedo += vec3(0.18, 0.03, 0.22) * ring * 0.25;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.8)), 0.05, 0.35,
                  0.55 + ring * 2.30 + vortex * 0.20);
)";

constexpr const char* kFallbackMembraneWeaveShader = R"(
float weaveA = smoothstep(0.5, 0.58, abs(sin(localUv.x * 16.0 + 1.5)));
float weaveB = smoothstep(0.5, 0.58, abs(sin(localUv.y * 16.0 + 1.5)));
float knots = fbm21(localUv * 16.0 + cellHash * 5.0);
float weave = max(weaveA, weaveB);
vec3 albedo = mix(base * 0.72, base * 1.10, weave);
albedo *= 0.90 + knots * 0.12;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.52, 0.22, 0.08 * weave);
)";

constexpr const char* kFallbackMembraneWireShader = R"(
float strand = smoothstep(0.08, 0.28, 1.0 - abs(centeredUv.x)) *
               smoothstep(0.08, 0.28, 1.0 - abs(centeredUv.y));
float pulse = 0.5 + 0.5 * sin(uTime * 7.0 + uv.y * 9.0 + cellHash * 5.0);
vec3 albedo = mix(base * 0.75, base * 1.20, strand);
albedo += vec3(0.08, 0.12, 0.08) * pulse * 0.18;
return makeSample(clamp(albedo, vec3(0.0), vec3(1.5)), 0.18, 0.32,
                  0.24 + strand * 0.22 + pulse * 0.10);
)";

std::string trimCopy(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string normalizeBlockIdValue(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       if (ch == ' ' || ch == '-') {
                           return static_cast<char>('_');
                       }
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string formatFloat(float value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

std::string formatVec3(const glm::vec3& value) {
    return "vec3(" + formatFloat(value.x) + ", " + formatFloat(value.y) + ", " +
           formatFloat(value.z) + ")";
}

std::uint32_t blockCategoryMask(std::initializer_list<BlockCategory> categories) {
    std::uint32_t mask = BLOCK_CATEGORY_NONE;
    for (const BlockCategory category : categories) {
        mask |= static_cast<std::uint32_t>(category);
    }
    return mask;
}

bool tryParseBlockCategory(const std::string& rawValue, BlockCategory& outCategory) {
    const std::string normalized = normalizeBlockIdValue(rawValue);
    if (normalized == "terrain") {
        outCategory = BlockCategory::TERRAIN;
        return true;
    }
    if (normalized == "decoration") {
        outCategory = BlockCategory::DECORATION;
        return true;
    }
    if (normalized == "portal") {
        outCategory = BlockCategory::PORTAL;
        return true;
    }
    if (normalized == "plant") {
        outCategory = BlockCategory::PLANT;
        return true;
    }
    if (normalized == "organic") {
        outCategory = BlockCategory::ORGANIC;
        return true;
    }

    outCategory = BlockCategory::NONE;
    return false;
}

struct BlockTextureAtlasData {
    std::string textureAssetPath{};
    glm::ivec2 gridSize{ 1, 1 };
    std::unordered_map<BlockId, glm::vec4> transformsByBlockId{};

    bool tryResolveTransform(BlockId blockId, glm::vec4& outTransform) const {
        const auto found = transformsByBlockId.find(blockId);
        if (found == transformsByBlockId.end()) {
            return false;
        }

        outTransform = found->second;
        return true;
    }
};

struct LoadedBlockTexture {
    BlockId blockId = BlockIds::AIR;
    std::filesystem::path sourcePath{};
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels{};
};

std::filesystem::path resolveBlockAssetPath(
    const std::filesystem::path& blockDirectory,
    const std::string& rawPath
) {
    if (rawPath.empty()) {
        return {};
    }

    const std::filesystem::path candidate(rawPath);
    if (candidate.is_absolute()) {
        return candidate;
    }

    if (!candidate.empty()) {
        const std::string firstSegment =
            normalizeBlockIdValue(candidate.begin()->generic_string());
        if (firstSegment == "assets" ||
            firstSegment == "res" ||
            firstSegment == "resources" ||
            firstSegment == "world" ||
            firstSegment == "engine" ||
            firstSegment == "saves" ||
            firstSegment == "presets") {
            return AppPaths::resolve(candidate);
        }
    }

    return (blockDirectory / candidate).lexically_normal();
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() &&
           std::filesystem::exists(path, ec) &&
           std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path detectDefaultBlockTexturePath(
    const std::filesystem::path& blockDirectory
) {
    const std::filesystem::path defaultTexturePath = blockDirectory / "texture.png";
    return fileExists(defaultTexturePath) ? defaultTexturePath : std::filesystem::path{};
}

bool tryLoadRgbaTexture(
    const std::filesystem::path& path,
    LoadedBlockTexture& outTexture
) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (data) {
            stbi_image_free(data);
        }
        return false;
    }

    outTexture.width = width;
    outTexture.height = height;
    outTexture.pixels.assign(data, data + (width * height * 4));
    stbi_image_free(data);
    return true;
}

void blitScaledTextureToAtlas(
    const LoadedBlockTexture& texture,
    std::vector<unsigned char>& atlasPixels,
    int atlasWidth,
    int atlasHeight,
    int destX,
    int destY,
    int tileWidth,
    int tileHeight
) {
    if (texture.width <= 0 || texture.height <= 0 ||
        tileWidth <= 0 || tileHeight <= 0 ||
        atlasWidth <= 0 || atlasHeight <= 0) {
        return;
    }

    for (int y = 0; y < tileHeight; ++y) {
        const int srcY = std::min(texture.height - 1, (y * texture.height) / tileHeight);
        const int atlasY = destY + y;
        if (atlasY < 0 || atlasY >= atlasHeight) {
            continue;
        }

        for (int x = 0; x < tileWidth; ++x) {
            const int srcX = std::min(texture.width - 1, (x * texture.width) / tileWidth);
            const int atlasX = destX + x;
            if (atlasX < 0 || atlasX >= atlasWidth) {
                continue;
            }

            const std::size_t srcIndex =
                static_cast<std::size_t>((srcY * texture.width + srcX) * 4);
            const std::size_t dstIndex =
                static_cast<std::size_t>((atlasY * atlasWidth + atlasX) * 4);

            atlasPixels[dstIndex + 0] = texture.pixels[srcIndex + 0];
            atlasPixels[dstIndex + 1] = texture.pixels[srcIndex + 1];
            atlasPixels[dstIndex + 2] = texture.pixels[srcIndex + 2];
            atlasPixels[dstIndex + 3] = texture.pixels[srcIndex + 3];
        }
    }
}

BlockTextureAtlasData buildGeneratedBlockTextureAtlas(
    const std::vector<BlockDefinition>& definitions
) {
    BlockTextureAtlasData atlas{};
    atlas.textureAssetPath =
        AppPaths::workspaceFile("artifacts/generated/block_textures/atlas.png").generic_string();

    std::vector<LoadedBlockTexture> loadedTextures;
    loadedTextures.reserve(definitions.size());

    int tileWidth = 1;
    int tileHeight = 1;

    for (const BlockDefinition& definition : definitions) {
        if (definition.textureAssetPath.empty()) {
            continue;
        }

        LoadedBlockTexture loadedTexture{};
        loadedTexture.blockId = definition.idValue;
        loadedTexture.sourcePath = definition.textureAssetPath;

        if (!tryLoadRgbaTexture(loadedTexture.sourcePath, loadedTexture)) {
            std::printf("[Blocks] Failed to load block texture: %s\n",
                        loadedTexture.sourcePath.string().c_str());
            continue;
        }

        tileWidth = std::max(tileWidth, loadedTexture.width);
        tileHeight = std::max(tileHeight, loadedTexture.height);
        loadedTextures.push_back(std::move(loadedTexture));
    }

    const int tileCount = std::max(1, static_cast<int>(loadedTextures.size()) + 1);
    const int columns =
        std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tileCount)))));
    const int rows = std::max(1, (tileCount + columns - 1) / columns);

    atlas.gridSize = glm::ivec2(columns, rows);

    const int atlasWidth = columns * tileWidth;
    const int atlasHeight = rows * tileHeight;
    std::vector<unsigned char> atlasPixels(
        static_cast<std::size_t>(atlasWidth * atlasHeight * 4),
        0
    );

    for (int y = 0; y < tileHeight; ++y) {
        for (int x = 0; x < tileWidth; ++x) {
            const std::size_t dstIndex =
                static_cast<std::size_t>((y * atlasWidth + x) * 4);
            atlasPixels[dstIndex + 0] = 255;
            atlasPixels[dstIndex + 1] = 255;
            atlasPixels[dstIndex + 2] = 255;
            atlasPixels[dstIndex + 3] = 255;
        }
    }

    for (std::size_t index = 0; index < loadedTextures.size(); ++index) {
        const int tileIndex = static_cast<int>(index) + 1;
        const int tileX = tileIndex % columns;
        const int tileY = tileIndex / columns;

        blitScaledTextureToAtlas(
            loadedTextures[index],
            atlasPixels,
            atlasWidth,
            atlasHeight,
            tileX * tileWidth,
            tileY * tileHeight,
            tileWidth,
            tileHeight
        );

        const glm::vec2 scale(
            1.0f / static_cast<float>(columns),
            1.0f / static_cast<float>(rows)
        );
        const glm::vec2 offset(
            static_cast<float>(tileX) * scale.x,
            1.0f - (static_cast<float>(tileY + 1) * scale.y)
        );

        atlas.transformsByBlockId[loadedTextures[index].blockId] =
            glm::vec4(scale, offset);
    }

    const std::filesystem::path atlasPath = atlas.textureAssetPath;
    std::error_code ec;
    std::filesystem::create_directories(atlasPath.parent_path(), ec);
    ec.clear();

    if (!stbi_write_png(atlasPath.string().c_str(),
                        atlasWidth,
                        atlasHeight,
                        4,
                        atlasPixels.data(),
                        atlasWidth * 4)) {
        std::printf("[Blocks] Failed to write generated block atlas: %s\n",
                    atlasPath.string().c_str());
    }

    return atlas;
}

const char* defaultShaderFor(std::string_view stableId) {
    if (stableId == "air") {
        return "return makeSample(base, 1.0, 0.0, 0.0);";
    }
    if (stableId == "stone") {
        return kFallbackStoneShader;
    }
    if (stableId == "crystal") {
        return kFallbackCrystalShader;
    }
    if (stableId == "void_matter") {
        return kFallbackVoidMatterShader;
    }
    if (stableId == "membrane") {
        return kFallbackMembraneShader;
    }
    if (stableId == "organic") {
        return kFallbackOrganicShader;
    }
    if (stableId == "metal") {
        return kFallbackMetalShader;
    }
    if (stableId == "portal") {
        return kFallbackPortalShader;
    }
    if (stableId == "membrane_weave") {
        return kFallbackMembraneWeaveShader;
    }
    if (stableId == "membrane_wire") {
        return kFallbackMembraneWireShader;
    }

    return "return makeSample(vec3(1.0, 0.0, 1.0), 1.0, 0.0, 0.0);";
}

BlockDefinition makeDefinition(
    BlockId blockId,
    const char* id,
    const char* displayName,
    std::uint32_t categoryMask,
    const BlockProperties& properties,
    const BlockData& data,
    bool replaceable,
    bool placeable,
    bool targetable,
    const glm::vec3& baseColor
) {
    BlockDefinition definition{};
    definition.idValue = blockId;
    definition.id = id;
    definition.displayName = displayName;
    definition.categoryMask = categoryMask;
    definition.properties = properties;
    definition.data = data;
    definition.replaceable = replaceable;
    definition.placeable = placeable;
    definition.targetable = targetable;
    definition.requiresTopPlacement = false;
    definition.baseColor = baseColor;
    definition.materialId = static_cast<int>(blockId);
    definition.selectionBounds = {};
    definition.shaderSource = defaultShaderFor(definition.id);
    return definition;
}

std::string makeDisplayNameFromStableId(std::string_view stableId) {
    std::string displayName;
    displayName.reserve(stableId.size());

    bool capitalizeNext = true;
    for (const char ch : stableId) {
        if (ch == '_' || ch == '-' || ch == ' ') {
            if (!displayName.empty() && displayName.back() != ' ') {
                displayName.push_back(' ');
            }
            capitalizeNext = true;
            continue;
        }

        if (capitalizeNext) {
            displayName.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            capitalizeNext = false;
        } else {
            displayName.push_back(ch);
        }
    }

    if (displayName.empty()) {
        return "Block";
    }

    return displayName;
}

BlockDefinition makeGenericFallbackDefinition(const BlockCatalogEntry& entry) {
    return makeDefinition(
        entry.value,
        entry.stableId.c_str(),
        makeDisplayNameFromStableId(entry.stableId).c_str(),
        blockCategoryMask({}),
        { true, false, false, false, false, BLOCK_TAG_NONE, 1.0f },
        { false, 0.0f },
        false,
        true,
        true,
        glm::vec3(1.0f)
    );
}

std::vector<BlockDefinition> makeFallbackDefinitions() {
    std::vector<BlockDefinition> definitions;
    definitions.reserve(BlockCatalog::count());

    for (const BlockCatalogEntry& entry : BlockCatalog::entries()) {
        BlockDefinition definition;

        if (entry.stableId == "air") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Air",
                blockCategoryMask({}),
                { false, false, false, false, false, BLOCK_TAG_NONE, 0.0f },
                { false, 0.0f },
                true,
                false,
                false,
                glm::vec3(0.0f)
            );
        } else if (entry.stableId == "stone") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Stone",
                blockCategoryMask({ BlockCategory::TERRAIN }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 7.5f },
                { false, 1.0f },
                false,
                true,
                true,
                glm::vec3(0.45f, 0.42f, 0.50f)
            );
        } else if (entry.stableId == "crystal") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Crystal",
                blockCategoryMask({ BlockCategory::TERRAIN, BlockCategory::DECORATION }),
                { true, false, false, true, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 250.0f },
                { false, 50.0f },
                false,
                true,
                true,
                glm::vec3(0.15f, 0.85f, 0.95f)
            );
        } else if (entry.stableId == "void_matter") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Void Matter",
                blockCategoryMask({ BlockCategory::TERRAIN }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 7.5f },
                { true, 2.0f },
                false,
                true,
                true,
                glm::vec3(0.20f, 0.08f, 0.30f)
            );
        } else if (entry.stableId == "membrane") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Membrane",
                blockCategoryMask({ BlockCategory::TERRAIN, BlockCategory::ORGANIC }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 0.75f },
                { true, 0.0f },
                false,
                true,
                true,
                glm::vec3(0.25f, 0.90f, 0.55f)
            );
        } else if (entry.stableId == "organic") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Organic",
                blockCategoryMask({ BlockCategory::TERRAIN, BlockCategory::ORGANIC }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 0.75f },
                { true, 0.0f },
                false,
                true,
                true,
                glm::vec3(0.75f, 0.35f, 0.28f)
            );
        } else if (entry.stableId == "metal") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Metal",
                blockCategoryMask({ BlockCategory::TERRAIN, BlockCategory::DECORATION }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_PICKAXE, 25.0f },
                { false, 0.0f },
                false,
                true,
                true,
                glm::vec3(0.68f, 0.70f, 0.75f)
            );
        } else if (entry.stableId == "portal") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Portal",
                blockCategoryMask({ BlockCategory::PORTAL, BlockCategory::DECORATION }),
                { true, false, false, true, false, BLOCK_TAG_NONE, 25.0f },
                { false, 0.0f },
                false,
                true,
                true,
                glm::vec3(0.95f, 0.20f, 0.85f)
            );
        } else if (entry.stableId == "membrane_weave") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Membrane Weave",
                blockCategoryMask({ BlockCategory::DECORATION, BlockCategory::ORGANIC }),
                { true, false, false, false, false, BLOCK_TAG_MINEABLE_WITH_AXE, 1.5f },
                { true, 1.0f },
                false,
                true,
                true,
                glm::vec3(0.58f, 0.92f, 0.78f)
            );
        } else if (entry.stableId == "membrane_wire") {
            definition = makeDefinition(
                entry.value,
                entry.stableId.c_str(),
                "Membrane Wire",
                blockCategoryMask(
                    { BlockCategory::PLANT, BlockCategory::DECORATION, BlockCategory::ORGANIC }
                ),
                { false, true, false, false, false, BLOCK_TAG_NONE, 0.01f },
                { true, 0.0f },
                true,
                true,
                true,
                glm::vec3(0.76f, 0.96f, 0.82f)
            );
            definition.hasCustomSelectionBounds = true;
            definition.requiresTopPlacement = true;
            definition.selectionBounds.min = glm::vec3(0.30f, 0.0f, 0.30f);
            definition.selectionBounds.max = glm::vec3(0.70f, 0.65f, 0.70f);
            definition.supportRule.mode = BlockSupportMode::ALLOW_LIST;
            definition.supportRule.allowedSupportTypes = { BlockIds::MEMBRANE };
            definition.customModelAssetPath = ClientAssets::kMembraneWireModelAsset;
            definition.topDecoration.enabled = true;
            definition.topDecoration.anchorBlockId = BlockIds::MEMBRANE;
            definition.topDecoration.requiredAboveBlockId = BlockIds::AIR;
            definition.topDecoration.spawnChance = 0.11f;
            definition.topDecoration.verticalOffset = 1;
            definition.topDecoration.seedKey = "membrane_wire";
        } else {
            definition = makeGenericFallbackDefinition(entry);
        }

        definitions.push_back(std::move(definition));
    }

    return definitions;
}

std::unordered_map<std::string, BlockId> makeFallbackIdMap(
    const std::vector<BlockDefinition>& definitions
) {
    std::unordered_map<std::string, BlockId> mapping;
    mapping.reserve(definitions.size());
    for (const BlockDefinition& definition : definitions) {
        mapping.emplace(normalizeBlockIdValue(definition.id), definition.idValue);
    }
    return mapping;
}

bool tryParseBlockIdValue(
    const json& value,
    const std::unordered_map<std::string, BlockId>& idMap,
    BlockId& outBlockId
) {
    if (!value.is_string()) {
        return false;
    }

    const std::string normalized = normalizeBlockIdValue(value.get<std::string>());
    const auto found = idMap.find(normalized);
    if (found == idMap.end()) {
        return false;
    }

    outBlockId = found->second;
    return true;
}

bool tryReadVec3(const json& value, glm::vec3& outValue) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }

    outValue = glm::vec3(
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>()
    );
    return true;
}

void overlayDefinitionFromJson(
    BlockDefinition& definition,
    const std::filesystem::path& blockDirectory,
    const std::unordered_map<std::string, BlockId>& idMap
) {
    const std::filesystem::path jsonPath = blockDirectory / "block.json";
    const std::string jsonSource = readTextFile(jsonPath);
    if (jsonSource.empty()) {
        return;
    }

    const json root = json::parse(jsonSource, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        std::printf("[Blocks] Failed to parse %s. Using fallback definition.\n",
                    jsonPath.string().c_str());
        return;
    }

    if (const json& idValue = root.value("id", json{}); idValue.is_string()) {
        const std::string loadedId = normalizeBlockIdValue(idValue.get<std::string>());
        if (loadedId != definition.id) {
            std::printf("[Blocks] Ignoring mismatched id '%s' for %s.\n",
                        loadedId.c_str(), definition.id.c_str());
        }
    }

    definition.displayName = root.value("display_name", definition.displayName);
    definition.replaceable = root.value("replaceable", definition.replaceable);
    definition.placeable = root.value("placeable", definition.placeable);
    definition.targetable = root.value("targetable", definition.targetable);
    definition.requiresTopPlacement =
        root.value("requires_top_placement", definition.requiresTopPlacement);
    definition.materialId = root.value("material_id", definition.materialId);

    if (const json& categoriesValue = root.value("categories", json{}); categoriesValue.is_array()) {
        std::uint32_t categoryMask = BLOCK_CATEGORY_NONE;
        for (const json& entry : categoriesValue) {
            if (!entry.is_string()) {
                continue;
            }

            BlockCategory parsedCategory = BlockCategory::NONE;
            if (tryParseBlockCategory(entry.get<std::string>(), parsedCategory)) {
                categoryMask |= static_cast<std::uint32_t>(parsedCategory);
            }
        }

        definition.categoryMask = categoryMask;
    }

    if (const json& propertiesValue = root.value("properties", json{}); propertiesValue.is_object()) {
        definition.properties.solid =
            propertiesValue.value("solid", definition.properties.solid);
        definition.properties.transparent =
            propertiesValue.value("transparent", definition.properties.transparent);
        definition.properties.interactable =
            propertiesValue.value("interactable", definition.properties.interactable);
        definition.properties.emitsLight =
            propertiesValue.value("emits_light", definition.properties.emitsLight);
        definition.properties.hasEntity =
            propertiesValue.value("has_entity", definition.properties.hasEntity);
        definition.properties.tags =
            propertiesValue.value("tags", definition.properties.tags);
        definition.properties.hardness =
            propertiesValue.value("hardness", definition.properties.hardness);
    }

    if (const json& dataValue = root.value("data", json{}); dataValue.is_object()) {
        definition.data.canDropItem =
            dataValue.value("can_drop_item", definition.data.canDropItem);
        definition.data.breakExperienceMultiplier =
            dataValue.value(
                "break_experience_multiplier",
                definition.data.breakExperienceMultiplier
            );
    }

    if (const json& renderValue = root.value("render", json{}); renderValue.is_object()) {
        definition.textureAssetPath.clear();
        definition.textureTileId.clear();

        glm::vec3 baseColor = definition.baseColor;
        if (tryReadVec3(renderValue.value("base_color", json{}), baseColor)) {
            definition.baseColor = baseColor;
        }

        const std::string textureAsset =
            renderValue.value("texture", renderValue.value("texture_asset", std::string{}));
        if (!textureAsset.empty()) {
            const std::filesystem::path resolvedTexturePath =
                resolveBlockAssetPath(blockDirectory, textureAsset);
            if (fileExists(resolvedTexturePath)) {
                definition.textureAssetPath = resolvedTexturePath.generic_string();
                definition.textureTileId = definition.id;
            } else {
                std::printf("[Blocks] Failed to find texture %s for %s.\n",
                            resolvedTexturePath.string().c_str(), definition.id.c_str());
            }
        }

        definition.customModelAssetPath =
            renderValue.value("custom_model_asset", definition.customModelAssetPath);

        if (const json& selectionValue =
                renderValue.value("selection_bounds", json{}); selectionValue.is_object()) {
            glm::vec3 minBounds = definition.selectionBounds.min;
            glm::vec3 maxBounds = definition.selectionBounds.max;
            const bool hasMin = tryReadVec3(selectionValue.value("min", json{}), minBounds);
            const bool hasMax = tryReadVec3(selectionValue.value("max", json{}), maxBounds);
            if (hasMin && hasMax) {
                definition.hasCustomSelectionBounds = true;
                definition.selectionBounds.min = minBounds;
                definition.selectionBounds.max = maxBounds;
            }
        }

        const std::string shaderAsset = renderValue.value("shader", std::string{});
        if (!shaderAsset.empty()) {
            const std::filesystem::path shaderPath =
                resolveBlockAssetPath(blockDirectory, shaderAsset);
            definition.shaderAssetPath = shaderPath.generic_string();
        }
    }

    if (definition.textureAssetPath.empty()) {
        const std::filesystem::path defaultTexturePath =
            detectDefaultBlockTexturePath(blockDirectory);
        if (!defaultTexturePath.empty()) {
            definition.textureAssetPath = defaultTexturePath.generic_string();
            definition.textureTileId = definition.id;
        }
    }

    if (const json& placementValue = root.value("placement", json{}); placementValue.is_object()) {
        const std::string supportMode =
            normalizeBlockIdValue(placementValue.value("support_mode", std::string{}));
        if (supportMode == "allow_list") {
            definition.supportRule.mode = BlockSupportMode::ALLOW_LIST;
        } else if (supportMode == "solid") {
            definition.supportRule.mode = BlockSupportMode::SOLID;
        }

        if (const json& allowedBlocks =
                placementValue.value("allowed_support_blocks", json{}); allowedBlocks.is_array()) {
            definition.supportRule.allowedSupportTypes.clear();
            for (const json& entry : allowedBlocks) {
                BlockId parsedBlockId = BlockIds::AIR;
                if (tryParseBlockIdValue(entry, idMap, parsedBlockId)) {
                    definition.supportRule.allowedSupportTypes.push_back(parsedBlockId);
                }
            }
        }
    }

    if (const json& generationValue = root.value("generation", json{}); generationValue.is_object()) {
        if (const json& topDecorationValue =
                generationValue.value("top_decoration", json{}); topDecorationValue.is_object()) {
            definition.topDecoration.enabled =
                topDecorationValue.value("enabled", definition.topDecoration.enabled);
            definition.topDecoration.spawnChance =
                topDecorationValue.value("spawn_chance", definition.topDecoration.spawnChance);
            definition.topDecoration.verticalOffset =
                topDecorationValue.value("vertical_offset", definition.topDecoration.verticalOffset);
            definition.topDecoration.seedKey =
                topDecorationValue.value("seed_key", definition.topDecoration.seedKey);

            BlockId anchorBlockId = definition.topDecoration.anchorBlockId;
            if (tryParseBlockIdValue(topDecorationValue.value("anchor_block", json{}),
                                     idMap, anchorBlockId)) {
                definition.topDecoration.anchorBlockId = anchorBlockId;
            }

            BlockId requiredAboveBlockId = definition.topDecoration.requiredAboveBlockId;
            if (tryParseBlockIdValue(topDecorationValue.value("required_above_block", json{}),
                                     idMap, requiredAboveBlockId)) {
                definition.topDecoration.requiredAboveBlockId = requiredAboveBlockId;
            }
        }
    }

    if (!definition.shaderAssetPath.empty()) {
        const std::string shaderSource = readTextFile(AppPaths::resolve(definition.shaderAssetPath));
        if (!shaderSource.empty()) {
            definition.shaderSource = shaderSource;
        } else {
            std::printf("[Blocks] Failed to read shader %s. Using fallback snippet.\n",
                        definition.shaderAssetPath.c_str());
        }
    }
}

std::string replaceToken(std::string source, const std::string& token, const std::string& value) {
    std::size_t offset = 0;
    while ((offset = source.find(token, offset)) != std::string::npos) {
        source.replace(offset, token.size(), value);
        offset += value.size();
    }
    return source;
}

} // namespace

const BlockRegistry& BlockRegistry::instance() {
    return mutableInstance();
}

BlockRegistry& BlockRegistry::mutableInstance() {
    static BlockRegistry registry;
    return registry;
}

BlockRegistry::BlockRegistry()
    : definitions_(makeFallbackDefinitions()) {
    reload();
}

void BlockRegistry::reload() {
    definitions_ = makeFallbackDefinitions();
    topDecorationDefinitions_.clear();
    textureAtlasAssetPath_.clear();

    const auto idMap = makeFallbackIdMap(definitions_);
    const std::filesystem::path blocksRoot = AppPaths::resolve(ClientAssets::kBlocksDirectory);

    for (BlockDefinition& definition : definitions_) {
        const BlockCatalogEntry* entry = BlockCatalog::findByValue(definition.idValue);
        const std::filesystem::path blockDirectory =
            blocksRoot / (entry ? entry->folderName : definition.id);
        overlayDefinitionFromJson(definition, blockDirectory, idMap);
    }

    const BlockTextureAtlasData atlas = buildGeneratedBlockTextureAtlas(definitions_);
    textureAtlasAssetPath_ = atlas.textureAssetPath;

    for (BlockDefinition& definition : definitions_) {
        definition.hasTextureTile =
            atlas.tryResolveTransform(definition.idValue, definition.atlasUvTransform);
        if (definition.hasTextureTile && definition.textureTileId.empty()) {
            definition.textureTileId = definition.id;
        }
        if (definition.topDecoration.enabled) {
            topDecorationDefinitions_.push_back(&definition);
        }
    }
}

const BlockDefinition& BlockRegistry::definition(BlockId blockId) const {
    const std::size_t index = static_cast<std::size_t>(blockId);
    if (index >= definitions_.size()) {
        return definitions_.front();
    }
    return definitions_[index];
}

const std::vector<BlockDefinition>& BlockRegistry::definitions() const {
    return definitions_;
}

const std::vector<const BlockDefinition*>& BlockRegistry::topDecorationDefinitions() const {
    return topDecorationDefinitions_;
}

const std::string& BlockRegistry::textureAtlasAssetPath() const {
    return textureAtlasAssetPath_;
}

bool BlockRegistry::tryParseId(const std::string& rawValue, BlockId& outBlockId) const {
    const std::string normalized = normalizeBlockIdValue(rawValue);
    for (const BlockDefinition& definition : definitions_) {
        if (normalized == normalizeBlockIdValue(definition.id)) {
            outBlockId = definition.idValue;
            return true;
        }
    }

    outBlockId = BlockIds::AIR;
    return false;
}

BlockShaderSources BlockRegistry::buildShaderSources() const {
    BlockShaderSources sources{};
    sources.vertexSource = readTextFile(AppPaths::resolve(ClientAssets::kBlockVertexShader));
    if (sources.vertexSource.empty()) {
        sources.error = "Failed to read block vertex shader file.";
        return sources;
    }

    const std::string fragmentTemplate =
        readTextFile(AppPaths::resolve(ClientAssets::kBlockFragmentShader));
    if (fragmentTemplate.empty()) {
        sources.error = "Failed to read block fragment shader template.";
        return sources;
    }

    std::ostringstream baseColorBuilder;
    std::ostringstream atlasSampleBuilder;
    std::ostringstream declarationsBuilder;
    std::ostringstream dispatchBuilder;
    std::ostringstream fallbackDeclarationsBuilder;
    std::ostringstream fallbackDispatchBuilder;

    for (const BlockDefinition& definition : definitions_) {
        baseColorBuilder
            << "    if (materialId == " << definition.materialId << ") {\n"
            << "        return " << formatVec3(definition.baseColor) << ";\n"
            << "    }\n";

        if (definition.hasTextureTile) {
            atlasSampleBuilder
                << "    if (materialId == " << definition.materialId << ") {\n"
                << "        vec2 atlasUv = vec2(" << formatFloat(definition.atlasUvTransform.z)
                << ", " << formatFloat(definition.atlasUvTransform.w) << ") + "
                << "localUv * vec2(" << formatFloat(definition.atlasUvTransform.x) << ", "
                << formatFloat(definition.atlasUvTransform.y) << ");\n"
                << "        return texture(uBlockAtlas, atlasUv);\n"
                << "    }\n";
        }

        declarationsBuilder
            << "MaterialSample sampleBlockMaterial_" << definition.id
            << "(vec3 worldPos, vec3 worldNormal, vec3 faceNormal, vec3 viewDir) {\n"
            << "    const int materialId = " << definition.materialId << ";\n"
            << "    vec3 base = blockBaseColor(materialId);\n"
            << "    vec2 uv = faceUv(worldPos, faceNormal);\n"
            << "    vec2 localUv = fract(uv + vec2(0.001));\n"
            << "    vec2 centeredUv = localUv - 0.5;\n"
            << "    vec4 atlasTexel = sampleBlockAtlas(materialId, localUv);\n"
            << "    vec3 atlasColor = atlasTexel.rgb;\n"
            << "    float atlasAlpha = atlasTexel.a;\n"
            << "    vec3 cell = floor(worldPos - faceNormal * 0.5 + vec3(0.001));\n"
            << "    float cellHash = hash31(cell + vec3(float(materialId) * 1.6180339, "
            << "2.13, 4.37));\n"
            << trimCopy(definition.shaderSource) << "\n"
            << "}\n\n";

        dispatchBuilder
            << "    if (materialId == " << definition.materialId << ") {\n"
            << "        return sampleBlockMaterial_" << definition.id
            << "(worldPos, worldNormal, faceNormal, viewDir);\n"
            << "    }\n";

        fallbackDeclarationsBuilder
            << "MaterialSample sampleBlockMaterial_" << definition.id
            << "(vec3 worldPos, vec3 worldNormal, vec3 faceNormal, vec3 viewDir) {\n"
            << "    const int materialId = " << definition.materialId << ";\n"
            << "    vec3 base = blockBaseColor(materialId);\n"
            << "    vec2 uv = faceUv(worldPos, faceNormal);\n"
            << "    vec2 localUv = fract(uv + vec2(0.001));\n"
            << "    vec4 atlasTexel = sampleBlockAtlas(materialId, localUv);\n"
            << "    vec3 albedo = base * atlasTexel.rgb;\n"
            << "    return makeSample(clamp(albedo, vec3(0.0), vec3(1.4)), 0.78, 0.10, 0.0);\n"
            << "}\n\n";

        fallbackDispatchBuilder
            << "    if (materialId == " << definition.materialId << ") {\n"
            << "        return sampleBlockMaterial_" << definition.id
            << "(worldPos, worldNormal, faceNormal, viewDir);\n"
            << "    }\n";
    }

    const auto buildFragment = [&](const std::string& declarationSource,
                                   const std::string& dispatchSource) {
        return replaceToken(
            replaceToken(
                replaceToken(
                    replaceToken(
                        fragmentTemplate,
                        "/*__BLOCK_BASE_COLOR__*/",
                        baseColorBuilder.str()
                    ),
                    "/*__BLOCK_ATLAS_SAMPLE__*/",
                    atlasSampleBuilder.str()
                ),
                "/*__BLOCK_SHADER_DECLARATIONS__*/",
                declarationSource
            ),
            "/*__BLOCK_SHADER_DISPATCH__*/",
            dispatchSource
        );
    };

    sources.fragmentSource =
        buildFragment(declarationsBuilder.str(), dispatchBuilder.str());
    sources.fallbackFragmentSource =
        buildFragment(fallbackDeclarationsBuilder.str(), fallbackDispatchBuilder.str());

    return sources;
}
